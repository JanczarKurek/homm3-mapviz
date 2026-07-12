/*
 * WallpaperRendererContext.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "WallpaperRendererContext.h"

#include <lib/Color.h>
#include <lib/Point.h>
#include <lib/int3.h>
#include <lib/mapping/CMap.h>

#include <array>
#include <cmath>

WallpaperRendererContext::WallpaperRendererContext(const MapRendererContextState & viewState)
	: MapRendererAdventureContext(viewState)
{
}

int3 WallpaperRendererContext::getMapSize() const
{
	return int3(map->width, map->height, map->levels());
}

bool WallpaperRendererContext::isInMap(const int3 & coordinates) const
{
	return map->isInTheMap(coordinates);
}

bool WallpaperRendererContext::isVisible(const int3 & coordinates) const
{
	// the whole map is revealed - a wallpaper has no fog of war
	return true;
}

bool WallpaperRendererContext::isActiveHero(const CGObjectInstance * obj) const
{
	return false;
}

int WallpaperRendererContext::attackedMonsterDirection(const CGObjectInstance * wanderingMonster) const
{
	return -1;
}

const TerrainTile & WallpaperRendererContext::getMapTile(const int3 & coordinates) const
{
	return map->getTile(coordinates);
}

const CGObjectInstance * WallpaperRendererContext::getObject(ObjectInstanceID objectID) const
{
	return map->getObject(objectID);
}

size_t WallpaperRendererContext::objectGroupIndex(ObjectInstanceID objectID) const
{
	// the walking hero uses the directional movement animation groups; everything else (and the
	// hero when idle) falls back to the adventure context's idle groups
	if(walkActive && objectID == target)
	{
		static const std::array<size_t, 9> moveGroups = {0, 10, 5, 6, 7, 8, 9, 12, 11};
		return moveGroups[getObjectRotation(objectID)];
	}
	return MapRendererAdventureContext::objectGroupIndex(objectID);
}

Point WallpaperRendererContext::objectImageOffset(ObjectInstanceID objectID, const int3 & coordinates) const
{
	// interpolate the moving hero's pixel offset between its source and destination tile so it
	// slides smoothly; the sprite is registered (addMovingObject) on every tile it covers, so
	// each clipped per-tile render composites the correct slice
	if(walkActive && objectID == target)
	{
		const Point from = Point(int3(tileFrom - coordinates)) * Point(32, 32);
		const Point dest = Point(int3(tileDest - coordinates)) * Point(32, 32);
		return Point(
			static_cast<int>(std::lround(from.x + (dest.x - from.x) * progress)),
			static_cast<int>(std::lround(from.y + (dest.y - from.y) * progress)));
	}
	return MapRendererAdventureContext::objectImageOffset(objectID, coordinates);
}

size_t WallpaperRendererContext::objectImageIndex(ObjectInstanceID objectID, size_t groupSize) const
{
	// the walk cycle advances faster (50ms/frame) than the idle/background animation (180ms)
	if(walkActive && objectID == target && groupSize > 0)
	{
		size_t frameCounter = animationTime / 50;
		return frameCounter % groupSize;
	}
	return MapRendererAdventureContext::objectImageIndex(objectID, groupSize);
}

const CGPath * WallpaperRendererContext::currentPath() const
{
	return showPath ? &walkPath : nullptr;
}

void WallpaperRendererContext::setWalkPath(const std::vector<int3> & remainingForward, const int3 & destTile)
{
	// Destination-first ordering, as MapRendererPath expects: node[0] is the target (drawn as the
	// cross marker), the remaining nodes run back toward the hero, and the last one is the hero's
	// current tile (drawn with no sprite). Every consecutive pair is a neighbour, so the in-between
	// nodes render as directional arrows.
	walkPath.nodes.clear();

	CGPathNode dest;
	dest.coord = destTile;
	dest.turns = 0;
	dest.action = EPathNodeAction::BLOCKING_VISIT;
	dest.accessible = EPathAccessibility::BLOCKVIS;
	walkPath.nodes.push_back(dest);

	for(auto it = remainingForward.rbegin(); it != remainingForward.rend(); ++it)
	{
		CGPathNode node;
		node.coord = *it;
		node.turns = 0;
		node.action = EPathNodeAction::NORMAL;
		node.accessible = EPathAccessibility::ACCESSIBLE;
		walkPath.nodes.push_back(node);
	}

	showPath = !remainingForward.empty();
}

void WallpaperRendererContext::clearWalkPath()
{
	showPath = false;
	walkPath.nodes.clear();
}

std::string WallpaperRendererContext::overlayText(const int3 & coordinates) const
{
	return {};
}

ColorRGBA WallpaperRendererContext::overlayTextColor(const int3 & coordinates) const
{
	return {};
}

bool WallpaperRendererContext::showSpellRange(const int3 & position) const
{
	return false;
}
