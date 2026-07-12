/*
 * WallpaperRendererContext.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <client/mapView/MapRendererContext.h>

#include <lib/pathfinder/CGPathNode.h>

VCMI_LIB_NAMESPACE_BEGIN
class CMap;
VCMI_LIB_NAMESPACE_END

/// Map-renderer context for the standalone wallpaper tool.
///
/// It reuses the in-game adventure context (so animationTime drives the same animated
/// water/lava/flags/objects the game shows), but overrides every accessor that would
/// otherwise reach into a live game (GAME->interface()/localState) and instead reads from
/// a plain, fully-loaded CMap. The whole map is treated as revealed - there is no fog,
/// no active hero, no movement paths and no overlays.
class WallpaperRendererContext : public MapRendererAdventureContext
{
public:
	const CMap * map = nullptr;

	/// Optional "hero-walk" state. When walkActive is set, the object `target` is drawn sliding
	/// from `tileFrom` to `tileDest` (anchor coordinates) by `progress` in [0,1], using the
	/// in-game walk animation - mirroring MapRendererAdventureMovingContext, which we cannot use
	/// directly because it inherits the live-game accessors this class overrides for headless use.
	bool walkActive = false;
	ObjectInstanceID target;
	int3 tileFrom;
	int3 tileDest;
	double progress = 0.0;

	/// The iconic H3 movement arrows, drawn by the engine's MapRendererPath from currentPath().
	/// Nodes are stored destination-first (node[0] = target -> cross marker, back() = hero tile).
	CGPath walkPath;
	bool showPath = false;
	/// Build the remaining arrow path: `remainingForward` runs from the hero's tile to the last
	/// walked tile, `destTile` is the object being approached (gets the destination cross).
	void setWalkPath(const std::vector<int3> & remainingForward, const int3 & destTile);
	void clearWalkPath();

	explicit WallpaperRendererContext(const MapRendererContextState & viewState);

	int3 getMapSize() const override;
	bool isInMap(const int3 & coordinates) const override;
	bool isVisible(const int3 & coordinates) const override;
	bool isActiveHero(const CGObjectInstance * obj) const override;
	int attackedMonsterDirection(const CGObjectInstance * wanderingMonster) const override;
	const TerrainTile & getMapTile(const int3 & coordinates) const override;
	const CGObjectInstance * getObject(ObjectInstanceID objectID) const override;
	size_t objectGroupIndex(ObjectInstanceID objectID) const override;
	Point objectImageOffset(ObjectInstanceID objectID, const int3 & coordinates) const override;
	size_t objectImageIndex(ObjectInstanceID objectID, size_t groupSize) const override;
	const CGPath * currentPath() const override;
	std::string overlayText(const int3 & coordinates) const override;
	ColorRGBA overlayTextColor(const int3 & coordinates) const override;
	bool showSpellRange(const int3 & position) const override;
};
