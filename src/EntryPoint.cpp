/*
 * EntryPoint.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

// vcmiwallpaper - renders a region of a Heroes III map as a seamlessly looping
// animated wallpaper (MP4), using the real in-game adventure-map renderer.

#include "StdInc.h"
#include <Global.h>

#include "WallpaperRendererContext.h"

#include <client/CMT.h>
#include <client/GameEngine.h>
#include <client/GameInstance.h>
#include <client/mapView/MapRenderer.h>
#include <client/mapView/MapRendererContextState.h>
#include <client/mapView/mapHandler.h>
#include <client/render/Canvas.h>
#include <client/render/CanvasImage.h>
#include <client/render/Graphics.h>
#include <client/render/IRenderHandler.h>

#include <lib/CConfigHandler.h>
#include <lib/CConsoleHandler.h>
#include <lib/GameLibrary.h>
#include <lib/LoadProgress.h>
#include <lib/Point.h>
#include <lib/Rect.h>
#include <lib/StartInfo.h>
#include <lib/VCMIDirs.h>
#include <lib/callback/GameRandomizer.h>
#include <lib/filesystem/CFilesystemLoader.h>
#include <lib/filesystem/Filesystem.h>
#include <lib/gameState/CGameState.h>
#include <lib/int3.h>
#include <lib/logging/CBasicLogConfigurator.h>
#include <lib/mapObjects/CGHeroInstance.h>
#include <lib/mapObjects/CGResource.h>
#include <lib/mapping/CMap.h>
#include <lib/mapping/CMapHeader.h>
#include <lib/mapping/CMapService.h>
#include <lib/mapping/TerrainTile.h>

#include <boost/program_options.hpp>

// Same version guard as client/ServerRunner.cpp.
#if BOOST_VERSION >= 108600
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/search_path.hpp>
#else
#include <boost/process/child.hpp>
#include <boost/process/search_path.hpp>
#endif

#include <SDL_stdinc.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <locale>
#include <map>
#include <queue>
#include <set>

namespace po = boost::program_options;
namespace bp = boost::process;

/// duration of a single adventure-map animation phase, must match
/// MapRendererAdventureContext::objectImageIndex / terrainImageIndex (baseFrameTime)
static constexpr int ANIMATION_FRAME_MS = 180;
static constexpr int TILE_SIZE = 32;

/// wall-clock duration of one emitted frame in hero-walk mode. Matches the in-game walk-cycle
/// frame time, so the default output fps is 1000/WALK_FRAME_MS = 20.
static constexpr int WALK_FRAME_MS = 50;

namespace
{
/// Register the directory containing the map file under the "mapEditor" filesystem so it can
/// be loaded by ResourcePath. CModHandler::findResourceOrigin only searches active mods, "core"
/// and "mapEditor", so that identifier is required for an arbitrary external map file.
ResourcePath registerMapResource(const boost::filesystem::path & mapFile)
{
	const std::string resourceName = "MAPEDITOR/" + mapFile.filename().string();
	ResourcePath resId(resourceName, EResType::MAP);

	auto loader = std::make_unique<CFilesystemLoader>("MAPEDITOR/", mapFile.parent_path().string(), 0);
	CResourceHandler::removeFilesystem("local", "mapEditor");
	CResourceHandler::addFilesystem("local", "mapEditor", std::move(loader));

	if(!CResourceHandler::get("mapEditor")->existsResource(resId))
		throw std::runtime_error("Cannot open map file: " + mapFile.string());

	return resId;
}

/// Render a single animation phase of a w x h-tile window whose top-left is at pixel
/// (originXpx, originYpx) on the map. The origin need not be tile-aligned: for smooth
/// (sub-tile) panning we render one extra row/column of tiles and crop the window out at
/// the sub-tile pixel offset.
std::shared_ptr<CanvasImage> renderFrame(MapRenderer & renderer, WallpaperRendererContext & context,
	int originXpx, int originYpx, int z, int w, int h)
{
	const int tileX0 = originXpx / TILE_SIZE;
	const int tileY0 = originYpx / TILE_SIZE;
	const int subX = originXpx % TILE_SIZE;
	const int subY = originYpx % TILE_SIZE;
	const int cols = w + (subX ? 1 : 0);
	const int rows = h + (subY ? 1 : 0);

	auto block = std::make_shared<CanvasImage>(Point(cols * TILE_SIZE, rows * TILE_SIZE), CanvasScalingPolicy::IGNORE);
	Canvas target = block->getCanvas();

	for(int ty = 0; ty < rows; ++ty)
	{
		for(int tx = 0; tx < cols; ++tx)
		{
			// each sub-renderer draws at (0,0) of the passed canvas, so hand it a
			// canvas clipped/offset to this tile's pixel position. Objects anchored
			// outside the region still paint correctly because addObject() registers
			// them on every tile their sprite covers - including the in-region tiles.
			Canvas tile(target, Rect(tx * TILE_SIZE, ty * TILE_SIZE, TILE_SIZE, TILE_SIZE));
			renderer.renderTile(context, tile, int3(tileX0 + tx, tileY0 + ty, z));
		}
	}

	if(subX == 0 && subY == 0)
		return block; // tile-aligned: the rendered block is exactly the window

	auto out = std::make_shared<CanvasImage>(Point(w * TILE_SIZE, h * TILE_SIZE), CanvasScalingPolicy::IGNORE);
	out->getCanvas().draw(block, Point(0, 0), Rect(subX, subY, w * TILE_SIZE, h * TILE_SIZE));
	return out;
}

/// parse "<int><sep><int>" (e.g. "1920x1080" or "80,60"); returns false on malformed input.
/// `separators` is the set of accepted separator characters.
bool parseIntPair(const std::string & s, const char * separators, int & a, int & b)
{
	const auto sep = s.find_first_of(separators);
	if(sep == std::string::npos)
		return false;
	try
	{
		size_t usedA = 0;
		size_t usedB = 0;
		a = std::stoi(s.substr(0, sep), &usedA);
		b = std::stoi(s.substr(sep + 1), &usedB);
		return usedA > 0 && usedB > 0;
	}
	catch(const std::exception &)
	{
		return false;
	}
}

/// Direction code (1..8) for a single-tile step src->dst, matching the engine's hero `moveDir`
/// encoding (see getDir in GameStatePackVisitor.cpp and idle/moveGroups in MapRendererContext):
///   1 2 3
///   8 . 4
///   7 6 5
/// Returns -1 if dst is not one of the 8 neighbours of src.
int walkDir(const int3 & src, const int3 & dst)
{
	const int dx = dst.x - src.x;
	const int dy = dst.y - src.y;
	if(dx == -1 && dy == -1) return 1;
	if(dx ==  0 && dy == -1) return 2;
	if(dx ==  1 && dy == -1) return 3;
	if(dx ==  1 && dy ==  0) return 4;
	if(dx ==  1 && dy ==  1) return 5;
	if(dx ==  0 && dy ==  1) return 6;
	if(dx == -1 && dy ==  1) return 7;
	if(dx == -1 && dy ==  0) return 8;
	return -1;
}

/// A tile a hero can stand on / pass through while strolling: on-map, passable (not rock), land
/// (no water), and not blocked by anything. Resource piles, monster guards and obstacles are all
/// blocking - the hero walks around them and (for piles) collects from an adjacent tile, exactly
/// as in-game, where pickables are block-visitable (CGResource sets blockVisit = true).
bool isWalkable(const CMap * map, const int3 & t)
{
	if(!map->isInTheMap(t))
		return false;
	const TerrainTile & tile = map->getTile(t);
	if(!tile.getTerrain()->isPassable())
		return false;
	if(!tile.isLand())
		return false;
	if(tile.blocked())
		return false;
	return true;
}

/// A tile rectangle in tile coordinates (inclusive). The walk can be confined to one so the hero,
/// piles and path all stay inside the rendered window.
struct TileRect
{
	int x0, y0, x1, y1;
	bool contains(const int3 & t) const { return t.x >= x0 && t.x <= x1 && t.y >= y0 && t.y <= y1; }
};

/// 8-connected BFS flood from `from` over walkable tiles inside `rect` (in *visitable* coordinates),
/// filling `prev` with parent pointers. `from` is seeded unconditionally (the hero stands on a
/// blocked tile - itself). One flood reaches every tile in the start's connected component, so the
/// whole tour is planned with one flood per leg, not one BFS per pile *pair*.
void bfsFlood(const CMap * map, const int3 & from, const TileRect & rect, std::map<int3, int3> & prev)
{
	static const int dx[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
	static const int dy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};

	std::queue<int3> q;
	q.push(from);
	prev[from] = from;

	while(!q.empty())
	{
		const int3 c = q.front();
		q.pop();
		for(int i = 0; i < 8; ++i)
		{
			const int3 n(c.x + dx[i], c.y + dy[i], c.z);
			if(prev.count(n))
				continue;
			if(!rect.contains(n) || !isWalkable(map, n))
				continue;
			prev[n] = c;
			q.push(n);
		}
	}
}

/// The 8 neighbours of `tile` inside `rect` that a hero can stand on (to visit a block-visitable
/// object there).
std::vector<int3> walkableNeighbours(const CMap * map, const int3 & tile, const TileRect & rect)
{
	static const int dx[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
	static const int dy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
	std::vector<int3> out;
	for(int i = 0; i < 8; ++i)
	{
		const int3 n(tile.x + dx[i], tile.y + dy[i], tile.z);
		if(rect.contains(n) && isWalkable(map, n))
			out.push_back(n);
	}
	return out;
}

/// Reconstruct the inclusive tile path from->to using parent pointers from bfsFlood, or empty if
/// `to` was never reached (consecutive tiles differ by <=1 in x and y, so walkDir is always valid).
std::vector<int3> reconstructPath(const std::map<int3, int3> & prev, const int3 & to)
{
	if(!prev.count(to))
		return {};
	std::vector<int3> path;
	for(int3 c = to;; c = prev.at(c))
	{
		path.push_back(c);
		if(c == prev.at(c))
			break;
	}
	std::reverse(path.begin(), path.end());
	return path;
}

/// Greedy nearest-neighbour tour of `piles` from `heroStart`, confined to `rect`. Resources are
/// block-visitable, so each leg's path ends on a walkable tile *beside* the pile. Returns ordered
/// (pile, path-to-approach) legs; piles with no reachable approach inside the rect are dropped.
std::vector<std::pair<CGResource *, std::vector<int3>>> buildTour(
	const CMap * map, const int3 & heroStart, const std::vector<CGResource *> & piles, const TileRect & rect)
{
	std::vector<std::pair<CGResource *, std::vector<int3>>> tour;
	int3 cur = heroStart;
	std::vector<CGResource *> remaining = piles;
	while(!remaining.empty())
	{
		std::map<int3, int3> prev;
		bfsFlood(map, cur, rect, prev);

		int bestIdx = -1;
		size_t bestLen = 0;
		std::vector<int3> bestPath;
		for(size_t i = 0; i < remaining.size(); ++i)
		{
			const int3 res = remaining[i]->visitablePos();
			std::vector<int3> approaches = walkableNeighbours(map, res, rect);
			if(cur != res && std::abs(cur.x - res.x) <= 1 && std::abs(cur.y - res.y) <= 1 && cur.z == res.z)
				approaches.push_back(cur);
			for(const int3 & approach : approaches)
			{
				auto path = reconstructPath(prev, approach);
				if(path.empty())
					continue;
				if(bestIdx < 0 || path.size() < bestLen)
				{
					bestIdx = static_cast<int>(i);
					bestLen = path.size();
					bestPath = std::move(path);
				}
			}
		}
		if(bestIdx < 0)
			break; // nothing else reachable on foot from here
		cur = bestPath.back(); // hero ends on the approach tile, beside the pile
		tour.emplace_back(remaining[bestIdx], std::move(bestPath));
		remaining.erase(remaining.begin() + bestIdx);
	}
	return tour;
}
} // namespace

// vcmiclientcommon expects the application to provide this (normally defined in clientapp).
// Headless variant: just log and abort, no SDL message box.
[[noreturn]] void handleFatalError(const std::string & message, bool terminate)
{
	logGlobal->error("FATAL ERROR ENCOUNTERED, VCMI WILL NOW TERMINATE");
	logGlobal->error("Reason: %s", message);
	std::cerr << "Fatal error! " << message << std::endl;

	if(terminate)
		throw std::runtime_error(message);
	else
		::exit(1);
}

int main(int argc, char * argv[])
{
	po::options_description opts("vcmiwallpaper - render a Heroes III map region as an animated wallpaper");
	opts.add_options()
		("help,h", "display help and exit")
		("map,m", po::value<std::string>(), "path to the map file (.h3m / .vmap) [required]")
		("x", po::value<int>()->default_value(0), "left tile of the region")
		("y", po::value<int>()->default_value(0), "top tile of the region")
		("width,w", po::value<int>()->default_value(0), "region width in tiles (0 = to map edge)")
		("height", po::value<int>()->default_value(0), "region height in tiles (0 = to map edge)")
		("scale,s", po::value<int>()->default_value(1), "integer nearest-neighbour upscale of the output (1 = native 32px tiles; e.g. 2 -> 64px tiles). Keeps pixels crisp and lets the wallpaper daemon avoid smooth-scaling")
		("resolution", po::value<std::string>(), "exact output pixel size, e.g. 1920x1080. Auto-picks the tiles needed to cover it (honouring --scale) and center-crops to the exact size; overrides --width/--height. Use --scale for zoom")
		("pan-to", po::value<std::string>(), "slow-pan mode: tile X,Y the view eases toward and back from (seamless ping-pong) over the loop, e.g. 80,60. --x/--y is the start, --width/--height the viewport (must be smaller than the map). More --frames = slower, smoother pan")
		("level,z", po::value<int>()->default_value(0), "map level (0 = surface, 1 = underground)")
		("frames,n", po::value<int>()->default_value(24), "number of 180ms animation phases to render = loop length. For a seamless loop use a multiple of the on-screen animations' periods; 24 (=LCM of the usual 4/6/8/12-frame H3 cycles) loops cleanly on most maps, 48 for a longer loop")
		("fps", po::value<double>()->default_value(1000.0 / ANIMATION_FRAME_MS), "output video framerate (walk mode defaults to 20 unless set)")
		("walk", "hero-walk mode: a hero tours the level and picks up every reachable resource pile (one-shot). Piles vanish as collected. By default the camera auto-frames the whole tour; pass --x/--y/--width/--height to fix the view, or --follow to track the hero")
		("follow", "walk mode: moving camera that keeps the hero centred (the map scrolls). Use --width/--height for the viewport size (default ~26 tiles). Alternative to the default auto-framed fixed view")
		("region", po::value<std::string>(), "walk mode: confine the hero, path and piles to a tile rectangle X,Y,W,H (e.g. 40,30,30,20). The camera is that rectangle; the hero and the piles it tours are picked from inside it")
		("hero", po::value<int>()->default_value(0), "walk mode: index of the hero to animate among those found on the level (0 = first; among heroes inside the region/window when one is set)")
		("step-frames", po::value<int>()->default_value(8), "walk mode: animation sub-frames per one-tile step (higher = smoother and slower)")
		("pickup-pause", po::value<int>()->default_value(6), "walk mode: frames to hold (hero idle) when a resource is collected")
		("out,o", po::value<std::string>()->default_value("wallpaper.webp"), "output path; format chosen by extension: .webp (animated, awww-compatible), .gif, .webm, .mp4")
		("ffmpeg", po::value<std::string>(), "path to the ffmpeg executable used to encode the frames (default: look it up on PATH)")
		("keep-frames", "keep the intermediate PNG frames");

	po::variables_map vm;
	try
	{
		po::store(po::parse_command_line(argc, argv, opts), vm);
		po::notify(vm);
	}
	catch(const std::exception & e)
	{
		std::cerr << "Error parsing command line: " << e.what() << std::endl;
		return 1;
	}

	if(vm.count("help") || !vm.count("map"))
	{
		std::cout << opts << std::endl;
		return vm.count("map") ? 0 : 1;
	}

	// Resolve user-provided paths to absolute now, before any working-directory change.
	const boost::filesystem::path mapFile = boost::filesystem::absolute(vm["map"].as<std::string>());
	const boost::filesystem::path outFile = boost::filesystem::absolute(vm["out"].as<std::string>());
	const int frameCount = std::max(1, vm["frames"].as<int>());
	const int scale = std::max(1, vm["scale"].as<int>());
	const bool keepFrames = vm.count("keep-frames") > 0;

	const bool walkMode = vm.count("walk") > 0;
	const int walkHeroIdx = vm["hero"].as<int>();
	const int walkStepFrames = std::max(1, vm["step-frames"].as<int>());
	const int walkPickupPause = std::max(0, vm["pickup-pause"].as<int>());

	// walk frames are spaced WALK_FRAME_MS apart, so play them back at that rate by default
	double fps = vm["fps"].as<double>();
	if(walkMode && vm["fps"].defaulted())
		fps = 1000.0 / WALK_FRAME_MS;

	// Validate geometry option formats up-front so a typo fails instantly, before the slow game
	// init. Map-dependent sizing/clamping happens later once the map is loaded.
	int outPxW = 0;
	int outPxH = 0;
	if(vm.count("resolution") && (!parseIntPair(vm["resolution"].as<std::string>(), "xX", outPxW, outPxH) || outPxW <= 0 || outPxH <= 0))
	{
		std::cerr << "Invalid --resolution (expected e.g. 1920x1080): " << vm["resolution"].as<std::string>() << std::endl;
		return 1;
	}
	const bool panMode = vm.count("pan-to") > 0;
	int panToX = 0;
	int panToY = 0;
	if(panMode && !parseIntPair(vm["pan-to"].as<std::string>(), ",", panToX, panToY))
	{
		std::cerr << "Invalid --pan-to (expected e.g. 80,60): " << vm["pan-to"].as<std::string>() << std::endl;
		return 1;
	}

	// Optional walk-mode region "X,Y,W,H" confining the hero, path and piles to a rectangle.
	const bool haveRegion = vm.count("region") > 0;
	int regionX = 0, regionY = 0, regionW = 0, regionH = 0;
	if(haveRegion && std::sscanf(vm["region"].as<std::string>().c_str(), "%d,%d,%d,%d", &regionX, &regionY, &regionW, &regionH) != 4)
	{
		std::cerr << "Invalid --region (expected X,Y,W,H, e.g. 40,30,30,20): " << vm["region"].as<std::string>() << std::endl;
		return 1;
	}
	if(haveRegion && (regionW <= 0 || regionH <= 0))
	{
		std::cerr << "Invalid --region: width and height must be positive: " << vm["region"].as<std::string>() << std::endl;
		return 1;
	}

	// Headless SDL: no display / audio device required. Honour any value the user already set.
	// SDL_setenv rather than setenv: the latter is POSIX-only and absent from the MSVC CRT.
	// The "offscreen" video driver is not built into SDL2 on Windows (only "dummy" and
	// "windows" are), and asking for it there makes SDL_Init fail outright.
#ifdef VCMI_WINDOWS
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 0);
#else
	SDL_setenv("SDL_VIDEODRIVER", "offscreen", 0);
#endif
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);

#if !defined(VCMI_MOBILE)
	// make executable-relative data paths resolvable, as the client does
	boost::filesystem::current_path(boost::filesystem::system_complete(argv[0]).parent_path());
#endif

	CConsoleHandler console;
	const boost::filesystem::path logPath = VCMIDirs::get().userLogsPath() / "VCMI_Wallpaper_log.txt";
	CBasicLogConfigurator logConfigurator(logPath, &console);
	logConfigurator.configureDefault();

	try
	{
		LIBRARY = new GameLibrary;
		LIBRARY->initializeFilesystem(false);
		logConfigurator.configure();

		// Force render settings that keep the offscreen path simple and the export 1:1
		{
			Settings fullscreen = settings.write["video"]["fullscreen"];
			fullscreen->Bool() = false;
			Settings vsync = settings.write["video"]["vsync"];
			vsync->Bool() = false;
			Settings filter = settings.write["video"]["upscalingFilter"];
			filter->String() = "none"; // guarantees getScalingFactor()==1 -> native 32px tiles
			Settings movePath = settings.write["adventure"]["showMovePath"];
			movePath->Bool() = true; // draw the hero's movement arrows in --walk mode
		}

		LIBRARY->initializeLibrary();

		ENGINE = std::make_unique<GameEngine>();
		GAME = std::make_unique<GameInstance>();

		graphics = new Graphics(); // player-color palettes used by the object renderer
		ENGINE->renderHandler().onLibraryLoadingFinished(LIBRARY);

		// Load the map directly from disk, bypassing the resource/mod system entirely.
		// Start a full (headless, all-AI) game on the map. Running CGameState::init resolves
		// random objects - random dwellings/towns/monsters/etc. - into concrete objects with
		// real appearances, exactly as the game does (the map editor leaves them as placeholders).
		CMapService mapService;
		const ResourcePath mapResource = registerMapResource(mapFile);
		const auto header = mapService.loadMapHeader(mapResource);

		StartInfo si;
		si.mode = EStartMode::NEW_GAME;
		si.difficulty = 1;
		si.mapname = "MAPEDITOR/" + mapFile.filename().string();
		for(size_t i = 0; i < header->players.size(); ++i)
		{
			const PlayerInfo & pinfo = header->players[i];
			if(!pinfo.canHumanPlay && !pinfo.canComputerPlay)
				continue;
			const PlayerColor color(static_cast<int>(i));
			PlayerSettings & pset = si.playerInfos[color];
			pset.color = color;
			pset.castle = pinfo.defaultCastle();
			pset.hero = pinfo.defaultHero();
			pset.compOnly = true; // headless: every player is AI (connectedPlayerIDs stays empty)
		}

		auto gameState = std::make_shared<CGameState>();
		gameState->preInit(LIBRARY);
		GameRandomizer randomizer(*gameState);
		Load::ProgressAccumulator progress;
		gameState->init(&mapService, &si, randomizer, progress, /*allowSavingRandomMap*/ false);

		CMap * map = &gameState->getMap();

		// Make GAME->map() resolve to the started map (needed by MapRendererContextState / renderer)
		GAME->setMapInstance(std::make_unique<CMapHandler>(map));

		// Resolve and clamp the requested region to the map bounds
		const int mapW = map->width;
		const int mapH = map->height;
		const int levels = map->levels();

		const int z = std::clamp(vm["level"].as<int>(), 0, levels - 1);

		// Optional exact output resolution (outPxW/outPxH, parsed earlier). H3 tiles are 32px, so an
		// arbitrary pixel size is rarely a whole number of tiles (1080/32 = 33.75): render enough
		// tiles to cover the requested pixels (honouring --scale) and ffmpeg center-crops/pads.
		int x0 = std::clamp(vm["x"].as<int>(), 0, mapW - 1);
		int y0 = std::clamp(vm["y"].as<int>(), 0, mapH - 1);

		int reqW = vm["width"].as<int>();
		int reqH = vm["height"].as<int>();
		if(outPxW > 0)
		{
			// tiles needed to cover the requested pixels at this scale (round up)
			const int denom = TILE_SIZE * scale;
			reqW = (outPxW + denom - 1) / denom;
			reqH = (outPxH + denom - 1) / denom;
			// pull the origin back so the whole span stays on the map when it can
			x0 = std::clamp(x0, 0, std::max(0, mapW - reqW));
			y0 = std::clamp(y0, 0, std::max(0, mapH - reqH));
		}
		const int w = (reqW <= 0) ? (mapW - x0) : std::min(reqW, mapW - x0);
		const int h = (reqH <= 0) ? (mapH - y0) : std::min(reqH, mapH - y0);

		logGlobal->info("Rendering region [%d,%d]+%dx%d at level %d (map is %dx%d, %d level(s))",
			x0, y0, w, h, z, mapW, mapH, levels);
		if(outPxW > 0 && (w < reqW || h < reqH))
			logGlobal->warn("Map region smaller than requested resolution - output will be padded with black bars");

		// Optional slow-pan ("--pan-to X,Y", parsed earlier into panToX/panToY): the view eases from
		// the start region to the target tile and back over the loop (cosine ping-pong) so it returns
		// to the start seamlessly. Movement is in pixels (sub-tile), clamped to keep the window on-map.
		const int startXpx = x0 * TILE_SIZE;
		const int startYpx = y0 * TILE_SIZE;
		int endXpx = startXpx;
		int endYpx = startYpx;
		if(panMode)
		{
			endXpx = std::clamp(panToX, 0, mapW - w) * TILE_SIZE;
			endYpx = std::clamp(panToY, 0, mapH - h) * TILE_SIZE;
			if(endXpx == startXpx && endYpx == startYpx)
				logGlobal->warn("--pan-to resolves to the start viewport (no room to pan?) - output will be static");
			else
				logGlobal->info("Panning view from pixel [%d,%d] to [%d,%d] and back", startXpx, startYpx, endXpx, endYpx);
		}
		const int maxXpx = (mapW - w) * TILE_SIZE;
		const int maxYpx = (mapH - h) * TILE_SIZE;

		// Build the renderer + context. State auto-populates from GAME->map().
		MapRendererContextState contextState;
		WallpaperRendererContext context(contextState);
		context.map = map;
		MapRenderer renderer;

		// Frames directory next to the output file
		const boost::filesystem::path framesDir = outFile.parent_path() / (outFile.stem().string() + "_frames");
		boost::filesystem::create_directories(framesDir);

		// Hero-walk mode: a hero strolls the level collecting every reachable resource pile, using
		// the real in-game walk animation. One-shot, fixed window. Falls back to the static/pan
		// render below if no suitable hero is found.
		bool ranWalk = false;
		if(walkMode)
		{
			// Pick a hero physically present on the chosen level (not garrisoned, not in a boat).
			std::vector<CGHeroInstance *> heroes;
			for(auto * h : map->getObjects<CGHeroInstance>())
				if(h->anchorPos().z == z && !h->isGarrisoned() && !h->inBoat())
					heroes.push_back(h);

			if(heroes.empty())
				logGlobal->warn("--walk: no free hero on level %d - falling back to a static render", z);
			else
			{
				const bool followCam = vm.count("follow") > 0;

				// Resource piles on this level (collected from the tile a hero stands on beside them).
				std::vector<CGResource *> piles;
				for(auto * r : map->getObjects<CGResource>())
					if(r->anchorPos().z == z)
						piles.push_back(r);

				const TileRect fullRect{0, 0, mapW - 1, mapH - 1};

				// Place a window of wW x wH centred on `centre`, kept on the map and always containing
				// `mustContain` (the hero), returning its top-left tile.
				auto placeWindow = [&](const int3 & centre, int wW, int wH, const int3 & mustContain) -> std::pair<int, int>
				{
					int wx = std::clamp(centre.x - wW / 2, mustContain.x - wW + 1, mustContain.x);
					int wy = std::clamp(centre.y - wH / 2, mustContain.y - wH + 1, mustContain.y);
					wx = std::clamp(wx, 0, std::max(0, mapW - wW));
					wy = std::clamp(wy, 0, std::max(0, mapH - wH));
					return {wx, wy};
				};

				// Does a fixed-size window bound the walk? --region, --resolution and --width/--height all
				// imply one. Without an explicit --x/--y (or --region) the window is auto-placed on the
				// hero with the most piles inside it.
				bool fixedWindow = false;
				int fixW = 0, fixH = 0;
				bool posGiven = false;
				int posX = 0, posY = 0;
				if(!followCam)
				{
					if(haveRegion)
					{
						fixW = regionW; fixH = regionH; posGiven = true; posX = regionX; posY = regionY;
					}
					else if(outPxW > 0)
					{
						fixW = reqW; fixH = reqH;
						if(!vm["x"].defaulted() || !vm["y"].defaulted()) { posGiven = true; posX = x0; posY = y0; }
					}
					else if(!vm["width"].defaulted() || !vm["height"].defaulted())
					{
						fixW = reqW > 0 ? reqW : mapW; fixH = reqH > 0 ? reqH : mapH;
						if(!vm["x"].defaulted() || !vm["y"].defaulted()) { posGiven = true; posX = x0; posY = y0; }
					}
					if(fixW > 0 && fixH > 0)
					{
						fixW = std::clamp(fixW, 1, mapW);
						fixH = std::clamp(fixH, 1, mapH);
						fixedWindow = true;
					}
				}

				CGHeroInstance * hero = nullptr;
				int winX0 = 0, winY0 = 0, winW = 0, winH = 0;
				std::vector<std::pair<CGResource *, std::vector<int3>>> tour;

				if(fixedWindow)
				{
					winW = fixW; winH = fixH;
					logGlobal->info("--walk: confining the walk to a %dx%d-tile window%s...",
						winW, winH, posGiven ? " at the requested position" : " (auto-placed on the best hero)");

					// Try (hero, window-position) candidates and keep whichever tours the most piles.
					int bestCount = -1;
					for(auto * h : heroes)
					{
						const int3 ht = h->visitablePos();
						std::vector<std::pair<int, int>> origins;
						if(posGiven)
						{
							const int wx = std::clamp(posX, 0, std::max(0, mapW - winW));
							const int wy = std::clamp(posY, 0, std::max(0, mapH - winH));
							if(ht.x >= wx && ht.x < wx + winW && ht.y >= wy && ht.y < wy + winH)
								origins.push_back({wx, wy});
						}
						else
						{
							// centre on the hero, and on the centroid of piles that could share its window
							origins.push_back(placeWindow(ht, winW, winH, ht));
							long sx = ht.x, sy = ht.y; int n = 1;
							for(auto * r : piles)
							{
								const int3 p = r->visitablePos();
								if(std::abs(p.x - ht.x) < winW && std::abs(p.y - ht.y) < winH)
								{ sx += p.x; sy += p.y; ++n; }
							}
							origins.push_back(placeWindow(int3(static_cast<int>(sx / n), static_cast<int>(sy / n), ht.z), winW, winH, ht));
						}
						for(const auto & [wx, wy] : origins)
						{
							const TileRect rect{wx, wy, wx + winW - 1, wy + winH - 1};
							std::vector<CGResource *> inWin;
							for(auto * r : piles)
								if(rect.contains(r->visitablePos()))
									inWin.push_back(r);
							auto t = buildTour(map, ht, inWin, rect);
							if(static_cast<int>(t.size()) > bestCount)
							{
								bestCount = static_cast<int>(t.size());
								hero = h; winX0 = wx; winY0 = wy; tour = std::move(t);
							}
						}
					}

					if(!hero)
					{
						// no hero fits the requested window: auto-place one on the first hero instead
						hero = heroes[std::clamp(walkHeroIdx, 0, static_cast<int>(heroes.size()) - 1)];
						const int3 ht = hero->visitablePos();
						const auto wpos = placeWindow(ht, winW, winH, ht);
						winX0 = wpos.first; winY0 = wpos.second;
						const TileRect rect{winX0, winY0, winX0 + winW - 1, winY0 + winH - 1};
						std::vector<CGResource *> inWin;
						for(auto * r : piles)
							if(rect.contains(r->visitablePos()))
								inWin.push_back(r);
						tour = buildTour(map, ht, inWin, rect);
						if(posGiven)
							logGlobal->warn("--walk: no hero inside the requested region - placed the window on hero #%d instead", hero->id.getNum());
					}

					logGlobal->info("--walk: hero #%d touring %d pile(s) inside window [%d,%d]+%dx%d (%dx%d px)",
						hero->id.getNum(), static_cast<int>(tour.size()), winX0, winY0, winW, winH, winW * TILE_SIZE, winH * TILE_SIZE);
				}
				else
				{
					// No fixed window: pick the hero, tour its whole connected component, then frame it.
					hero = heroes[std::clamp(walkHeroIdx, 0, static_cast<int>(heroes.size()) - 1)];
					tour = buildTour(map, hero->visitablePos(), piles, fullRect);
					logGlobal->info("--walk: hero #%d touring %d of %d resource pile(s) on level %d",
						hero->id.getNum(), static_cast<int>(tour.size()), static_cast<int>(piles.size()), z);

					if(followCam)
					{
						winW = std::min(reqW > 0 ? reqW : 26, mapW);
						winH = std::min(reqH > 0 ? reqH : 26, mapH);
						winX0 = 0; winY0 = 0; // origin recomputed per frame
						logGlobal->info("--walk: follow camera, %dx%d-tile viewport (%dx%d px)",
							winW, winH, winW * TILE_SIZE, winH * TILE_SIZE);
					}
					else
					{
						// auto-frame: bounding box of the hero start + all tour tiles + the piles, + margin
						int minX = hero->visitablePos().x, maxX = minX;
						int minY = hero->visitablePos().y, maxY = minY;
						auto accumulate = [&](const int3 & t)
						{
							minX = std::min(minX, t.x); maxX = std::max(maxX, t.x);
							minY = std::min(minY, t.y); maxY = std::max(maxY, t.y);
						};
						for(const auto & [pile, pathV] : tour)
						{
							accumulate(pile->visitablePos());
							for(const int3 & t : pathV)
								accumulate(t);
						}
						const int margin = 4;
						winX0 = std::clamp(minX - margin, 0, mapW - 1);
						winY0 = std::clamp(minY - margin, 0, mapH - 1);
						winW = std::min(maxX + margin, mapW - 1) - winX0 + 1;
						winH = std::min(maxY + margin, mapH - 1) - winY0 + 1;
						logGlobal->info("--walk: auto-framed the tour to region [%d,%d]+%dx%d (%dx%d px) - pass --region/--resolution to bound it, or --follow to track the hero",
							winX0, winY0, winW, winH, winW * TILE_SIZE, winH * TILE_SIZE);
					}
				}
				winW = std::clamp(winW, 1, mapW - winX0);
				winH = std::clamp(winH, 1, mapH - winY0);

				// pixel origin that keeps a given anchor-tile centre in view, clamped on-map (follow cam)
				auto followOrigin = [&](double tileX, double tileY, int & ox, int & oy)
				{
					ox = std::clamp(static_cast<int>(std::lround((tileX + 0.5) * TILE_SIZE - winW * TILE_SIZE / 2.0)), 0, (mapW - winW) * TILE_SIZE);
					oy = std::clamp(static_cast<int>(std::lround((tileY + 0.5) * TILE_SIZE - winH * TILE_SIZE / 2.0)), 0, (mapH - winH) * TILE_SIZE);
				};

				// Total frames known up-front, so we can show real progress while rendering silently.
				size_t totalFrames = 0;
				for(const auto & [pile, pathV] : tour)
					totalFrames += (pathV.size() > 1 ? (pathV.size() - 1) * walkStepFrames : 0) + walkPickupPause;
				if(totalFrames == 0)
					totalFrames = 1;
				logGlobal->info("--walk: rendering %d frame(s) at %dx%d px (this may take a while)...",
					static_cast<int>(totalFrames), winW * TILE_SIZE, winH * TILE_SIZE);

				context.target = hero->id;
				uint32_t animT = 0;
				int frameIdx = 0;
				auto emit = [&](int ox, int oy)
				{
					auto image = renderFrame(renderer, context, ox, oy, z, winW, winH);
					char name[32];
					std::snprintf(name, sizeof(name), "frame_%04d.png", frameIdx);
					image->exportBitmap(framesDir / name);
					++frameIdx;
					// progress heartbeat - one line every ~25 frames (and the final frame)
					if(frameIdx % 25 == 0 || static_cast<size_t>(frameIdx) == totalFrames)
						logGlobal->info("--walk: rendered %d / %d frames", frameIdx, static_cast<int>(totalFrames));
				};

				size_t pileNum = 0;
				for(const auto & [pile, pathV] : tour)
				{
					++pileNum;
					const int3 resTile = pile->visitablePos();
					logGlobal->info("--walk: leg %d/%d - walking %d tile(s), then collecting the pile from beside it",
						static_cast<int>(pileNum), static_cast<int>(tour.size()),
						static_cast<int>(pathV.empty() ? 0 : pathV.size() - 1));
					for(size_t i = 0; i + 1 < pathV.size(); ++i)
					{
						const int3 aFrom = hero->convertFromVisitablePos(pathV[i]);
						const int3 aDest = hero->convertFromVisitablePos(pathV[i + 1]);
						const int dir = walkDir(aFrom, aDest);
						if(dir > 0)
							hero->moveDir = static_cast<ui8>(dir);

						// the iconic movement arrows: the route still ahead (tiles i+1..end) ending in
						// the pile, which gets the destination cross
						context.setWalkPath(std::vector<int3>(pathV.begin() + i + 1, pathV.end()), resTile);

						// register the hero across both tiles so the clipped per-tile render
						// composites a sprite that spans them as it slides
						contextState.removeObject(hero);
						contextState.addMovingObject(hero, aFrom, aDest);
						context.walkActive = true;
						context.tileFrom = aFrom;
						context.tileDest = aDest;

						for(int s = 0; s < walkStepFrames; ++s)
						{
							context.progress = static_cast<double>(s) / walkStepFrames;
							context.animationTime = animT;
							animT += WALK_FRAME_MS;
							int ox = winX0 * TILE_SIZE, oy = winY0 * TILE_SIZE;
							if(followCam)
								followOrigin(aFrom.x + (aDest.x - aFrom.x) * context.progress,
									aFrom.y + (aDest.y - aFrom.y) * context.progress, ox, oy);
							emit(ox, oy);
						}

						// commit the step: the hero now stands on aDest
						contextState.removeObject(hero);
						hero->setAnchorPos(aDest);
						contextState.addObject(hero);
					}

					// the hero now stands BESIDE the pile: face it and "visit" (a move action that
					// doesn't change position), then the pile is collected and vanishes - like in-game.
					const int3 approach = pathV.back();
					const int faceDir = walkDir(approach, resTile);
					if(faceDir > 0)
						hero->moveDir = static_cast<ui8>(faceDir);
					context.walkActive = false;          // idle pose, no sliding
					context.setWalkPath({approach}, resTile); // just the destination cross on the pile

					int ox = winX0 * TILE_SIZE, oy = winY0 * TILE_SIZE;
					if(followCam)
					{
						const int3 a = hero->anchorPos();
						followOrigin(a.x, a.y, ox, oy);
					}
					const int collectAt = std::max(1, walkPickupPause / 2);
					for(int p = 0; p < walkPickupPause; ++p)
					{
						if(p == collectAt)
						{
							contextState.removeObject(pile); // collected: the pile disappears
							context.clearWalkPath();          // and so does its arrow/cross
						}
						context.animationTime = animT;
						animT += WALK_FRAME_MS;
						emit(ox, oy);
					}
					context.clearWalkPath();
				}

				if(frameIdx == 0)
				{
					// nowhere to walk and nothing to collect: emit one idle frame so ffmpeg has input
					context.walkActive = false;
					context.clearWalkPath();
					context.animationTime = 0;
					int ox = winX0 * TILE_SIZE, oy = winY0 * TILE_SIZE;
					if(followCam)
					{
						const int3 a = hero->anchorPos();
						followOrigin(a.x, a.y, ox, oy);
					}
					emit(ox, oy);
				}

				logGlobal->info("--walk: rendered %d frame(s)", frameIdx);
				ranWalk = true;
			}
		}

		if(!ranWalk)
		{
			constexpr double TWO_PI = 6.283185307179586;
			for(int frame = 0; frame < frameCount; ++frame)
			{
				context.animationTime = static_cast<uint32_t>(frame * ANIMATION_FRAME_MS);

				int originXpx = startXpx;
				int originYpx = startYpx;
				if(panMode)
				{
					// cosine ease in/out: 0 at frame 0, 1 at the midpoint, 0 again at frameCount
					// (the unrendered wrap frame), so velocity is zero at both the loop point and turn.
					const double t = (1.0 - std::cos(TWO_PI * frame / frameCount)) / 2.0;
					originXpx = std::clamp(static_cast<int>(std::lround(startXpx + (endXpx - startXpx) * t)), 0, maxXpx);
					originYpx = std::clamp(static_cast<int>(std::lround(startYpx + (endYpx - startYpx) * t)), 0, maxYpx);
				}
				auto image = renderFrame(renderer, context, originXpx, originYpx, z, w, h);

				char name[32];
				std::snprintf(name, sizeof(name), "frame_%04d.png", frame);
				image->exportBitmap(framesDir / name);
				logGlobal->info("Rendered frame %d / %d", frame + 1, frameCount);
			}
		}

		// Assemble a seamlessly looping animation with ffmpeg. Frames 0..N-1 form one full
		// animation cycle, so looping the output wraps last->first with no jump.
		// The encoder is chosen from the output extension; default .webp is what the awww
		// wayland wallpaper daemon (and other image-based tools) can display directly.
		std::string ext = outFile.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

		const std::string inputPattern = (framesDir / "frame_%04d.png").string();

		// Pixel art must never be smooth-scaled or chroma-subsampled or it goes blurry, so every
		// format below is encoded losslessly with full 4:4:4 colour, and the optional integer
		// upscale uses nearest-neighbour ("neighbor") - the grid stays snapped to its pixelation.
		// geomVf is the geometry filter prefix (upscale, then exact-size crop/pad); each filter
		// keeps a trailing ',' so it can be prepended to a per-codec filterchain.
		std::string geomVf;
		if(scale > 1)
			geomVf += "scale=iw*" + std::to_string(scale) + ":ih*" + std::to_string(scale) + ":flags=neighbor,";
		if(outPxW > 0)
		{
			// crop down to (at most) the target, then pad up to it - yields exactly outPxW x outPxH,
			// centred; black bars appear only if the rendered region was smaller (near a map edge).
			// Commas inside min() are escaped so ffmpeg does not read them as filterchain separators.
			const std::string W = std::to_string(outPxW);
			const std::string H = std::to_string(outPxH);
			geomVf += "crop=min(iw\\," + W + "):min(ih\\," + H + "),"
				+ "pad=" + W + ":" + H + ":(ow-iw)/2:(oh-ih)/2:color=black,";
		}

		// ffmpeg is invoked directly, not through a shell: arguments are passed as a plain argv
		// vector and boost::process does the platform-appropriate escaping. (A shell command
		// string would need different quoting on cmd.exe than on sh, and neither would survive
		// a path containing spaces unscathed.)
		//
		// The framerate is formatted through an explicitly classic-locale stream so that it stays
		// "5.55556" and never becomes "5,55556" - ffmpeg would reject the latter.
		std::ostringstream fpsStream;
		fpsStream.imbue(std::locale::classic());
		fpsStream << fps;

		std::vector<std::string> args{"-y", "-framerate", fpsStream.str(), "-i", inputPattern};
		if(ext == ".webp")
		{
			// lossless animated WebP, loops forever; consumed directly by awww
			if(!geomVf.empty())
				args.insert(args.end(), {"-vf", geomVf.substr(0, geomVf.size() - 1)});
			args.insert(args.end(), {"-c:v", "libwebp_anim", "-lossless", "1", "-loop", "0", "-an"});
		}
		else if(ext == ".gif")
		{
			// dither=none keeps flat pixel-art colour crisp within the 256-colour limit
			args.insert(args.end(), {"-vf",
				geomVf + "split[a][b];[a]palettegen=max_colors=256:stats_mode=diff[p];[b][p]paletteuse=dither=none"});
			args.insert(args.end(), {"-loop", "0"});
		}
		else if(ext == ".webm")
		{
			// VP9 lossless, full 4:4:4 chroma (no colour bleeding on sprites)
			args.insert(args.end(), {"-vf", geomVf + "format=yuv444p"});
			args.insert(args.end(), {"-c:v", "libvpx-vp9", "-lossless", "1", "-an"});
		}
		else // .mp4 / fallback: H.264 lossless (qp 0), full 4:4:4 chroma
		{
			args.insert(args.end(), {"-vf", geomVf + "format=yuv444p"});
			args.insert(args.end(), {"-c:v", "libx264", "-qp", "0", "-preset", "veryslow", "-movflags", "+faststart"});
		}
		args.push_back(outFile.string());

		const boost::filesystem::path ffmpegPath = vm.count("ffmpeg")
			? boost::filesystem::path(vm["ffmpeg"].as<std::string>())
			: bp::search_path("ffmpeg");

		if(ffmpegPath.empty())
		{
			std::cerr << "ffmpeg was not found on PATH. Install it, or point --ffmpeg at the executable.\n"
				<< "The PNG frames have been kept, in: " << framesDir.string() << std::endl;
			return 2;
		}

		std::ostringstream shownCmd;
		shownCmd << ffmpegPath.string();
		for(const auto & arg : args)
			shownCmd << ' ' << arg;
		logGlobal->info("Encoding video: %s", shownCmd.str());

		std::error_code ec;
		bp::child ffmpeg(ffmpegPath, args, ec);
		if(ec)
		{
			std::cerr << "Could not run ffmpeg (" << ffmpegPath.string() << "): " << ec.message() << "\n"
				<< "The PNG frames have been kept, in: " << framesDir.string() << std::endl;
			return 2;
		}
		ffmpeg.wait();

		const int rc = ffmpeg.exit_code();
		if(rc != 0)
		{
			std::cerr << "ffmpeg failed (exit code " << rc << "). The PNG frames are in: "
				<< framesDir.string() << std::endl;
			return 2;
		}

		if(!keepFrames)
			boost::filesystem::remove_all(framesDir);

		std::cout << "Wallpaper written to: " << outFile.string() << std::endl;
	}
	catch(const std::exception & e)
	{
		logGlobal->error("Fatal error: %s", e.what());
		std::cerr << "Fatal error: " << e.what() << std::endl;
		return 1;
	}

	GAME.reset();
	ENGINE.reset();
	logConfigurator.deconfigure();
	delete LIBRARY;
	LIBRARY = nullptr;
	return 0;
}
