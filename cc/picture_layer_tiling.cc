// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/picture_layer_tiling.h"

#include "base/debug/trace_event.h"
#include "cc/math_util.h"
#include "ui/gfx/point_conversions.h"
#include "ui/gfx/rect_conversions.h"
#include "ui/gfx/safe_integer_conversions.h"
#include "ui/gfx/size_conversions.h"

namespace cc {

scoped_ptr<PictureLayerTiling> PictureLayerTiling::Create(
    float contents_scale) {
  return make_scoped_ptr(new PictureLayerTiling(contents_scale));
}

scoped_ptr<PictureLayerTiling> PictureLayerTiling::Clone() const {
  return make_scoped_ptr(new PictureLayerTiling(*this));
}

PictureLayerTiling::PictureLayerTiling(float contents_scale)
    : client_(NULL),
      contents_scale_(contents_scale),
      tiling_data_(gfx::Size(), gfx::Size(), true),
      resolution_(NON_IDEAL_RESOLUTION) {
}

PictureLayerTiling::~PictureLayerTiling() {
}

void PictureLayerTiling::SetClient(PictureLayerTilingClient* client) {
  client_ = client;
}

gfx::Rect PictureLayerTiling::ContentRect() const {
  gfx::Size content_bounds =
      gfx::ToCeiledSize(gfx::ScaleSize(layer_bounds_, contents_scale_));
  return gfx::Rect(gfx::Point(), content_bounds);
}

gfx::SizeF PictureLayerTiling::ContentSizeF() const {
  return gfx::ScaleSize(layer_bounds_, contents_scale_);
}

Tile* PictureLayerTiling::TileAt(int i, int j) const {
  TileMap::const_iterator iter = tiles_.find(TileMapKey(i, j));
  if (iter == tiles_.end())
    return NULL;
  return iter->second.get();
}

void PictureLayerTiling::CreateTile(int i, int j) {
  gfx::Rect tile_rect = tiling_data_.TileBoundsWithBorder(i, j);
  tile_rect.set_size(tiling_data_.max_texture_size());
  TileMapKey key(i, j);
  DCHECK(tiles_.find(key) == tiles_.end());
  scoped_refptr<Tile> tile = client_->CreateTile(this, tile_rect);
  if (tile)
    tiles_[key] = tile;
}

Region PictureLayerTiling::OpaqueRegionInContentRect(
    const gfx::Rect& content_rect) const {
  Region opaque_region;
  // TODO(enne): implement me
  return opaque_region;
}

void PictureLayerTiling::SetLayerBounds(gfx::Size layer_bounds) {
  if (layer_bounds_ == layer_bounds)
    return;

  gfx::Size old_layer_bounds = layer_bounds_;
  layer_bounds_ = layer_bounds;
  gfx::Size old_content_bounds = tiling_data_.total_size();
  gfx::Size content_bounds =
      gfx::ToCeiledSize(gfx::ScaleSize(layer_bounds_, contents_scale_));

  tiling_data_.SetTotalSize(content_bounds);
  if (layer_bounds_.IsEmpty()) {
    tiles_.clear();
    return;
  }

  gfx::Size tile_size = client_->CalculateTileSize(
      tiling_data_.max_texture_size(),
      content_bounds);
  if (tile_size != tiling_data_.max_texture_size()) {
    tiling_data_.SetMaxTextureSize(tile_size);
    tiles_.clear();
  }

  // Any tiles outside our new bounds are invalid and should be dropped.
  if (old_content_bounds.width() > content_bounds.width() ||
      old_content_bounds.height() > content_bounds.height()) {
    int right =
        tiling_data_.TileXIndexFromSrcCoord(content_bounds.width() - 1);
    int bottom =
        tiling_data_.TileYIndexFromSrcCoord(content_bounds.height() - 1);

    std::vector<TileMapKey> invalid_tile_keys;
    for (TileMap::const_iterator it = tiles_.begin();
         it != tiles_.end(); ++it) {
      if (it->first.first > right || it->first.second > bottom)
        invalid_tile_keys.push_back(it->first);
    }
    for (size_t i = 0; i < invalid_tile_keys.size(); ++i)
      tiles_.erase(invalid_tile_keys[i]);
  }

  // Create tiles for newly exposed areas.
  Region layer_region((gfx::Rect(layer_bounds_)));
  layer_region.Subtract(gfx::Rect(old_layer_bounds));
  for (Region::Iterator iter(layer_region); iter.has_rect(); iter.next()) {
    Invalidate(iter.rect());
    CreateTilesFromLayerRect(iter.rect());
  }
}

void PictureLayerTiling::Invalidate(const Region& layer_invalidation) {
  std::vector<TileMapKey> new_tiles;

  for (Region::Iterator region_iter(layer_invalidation);
       region_iter.has_rect();
       region_iter.next()) {

    gfx::Rect layer_invalidation = region_iter.rect();
    layer_invalidation.Intersect(gfx::Rect(layer_bounds_));
    gfx::Rect rect =
        gfx::ToEnclosingRect(ScaleRect(layer_invalidation, contents_scale_));

    for (PictureLayerTiling::Iterator tile_iter(this, contents_scale_, rect);
         tile_iter;
         ++tile_iter) {
      TileMapKey key(tile_iter.tile_i_, tile_iter.tile_j_);
      TileMap::iterator found = tiles_.find(key);
      if (found == tiles_.end())
        continue;

      tiles_.erase(found);
      new_tiles.push_back(key);
    }
  }

  for (size_t i = 0; i < new_tiles.size(); ++i)
    CreateTile(new_tiles[i].first, new_tiles[i].second);
}

void PictureLayerTiling::CreateTilesFromLayerRect(gfx::Rect layer_rect) {
  gfx::Rect content_rect =
      gfx::ToEnclosingRect(ScaleRect(layer_rect, contents_scale_));
  CreateTilesFromContentRect(content_rect);
}

void PictureLayerTiling::CreateTilesFromContentRect(gfx::Rect content_rect) {
  for (TilingData::Iterator iter(&tiling_data_, content_rect); iter; ++iter) {
    TileMap::iterator found =
        tiles_.find(TileMapKey(iter.index_x(), iter.index_y()));
    // Ignore any tiles that already exist.
    if (found != tiles_.end())
      continue;
    CreateTile(iter.index_x(), iter.index_y());
  }
}

PictureLayerTiling::Iterator::Iterator()
    : tiling_(NULL),
      current_tile_(NULL),
      tile_i_(0),
      tile_j_(0),
      left_(0),
      top_(0),
      right_(-1),
      bottom_(-1) {
}

PictureLayerTiling::Iterator::Iterator(const PictureLayerTiling* tiling,
                                       float dest_scale,
                                       gfx::Rect dest_rect)
    : tiling_(tiling),
      dest_rect_(dest_rect),
      current_tile_(NULL),
      dest_to_content_scale_x_(0),
      dest_to_content_scale_y_(0),
      tile_i_(0),
      tile_j_(0),
      left_(0),
      top_(0),
      right_(-1),
      bottom_(-1) {
  DCHECK(tiling_);
  if (dest_rect_.IsEmpty())
    return;

  float dest_to_content_scale = tiling_->contents_scale_ / dest_scale;
  dest_to_content_scale_x_ = dest_to_content_scale;
  dest_to_content_scale_y_ = dest_to_content_scale;

  // Do not draw last row/column of texels if they don't have enough
  // rasterization coverage. i.e.: the ceiled content size does not equal the
  // floored size.
  gfx::SizeF content_size = tiling_->ContentSizeF();
  gfx::Size content_size_ceil = gfx::ToCeiledSize(content_size);
  gfx::Size content_size_floor = gfx::ToFlooredSize(content_size);

  if (content_size_floor.width() != content_size_ceil.width())
    dest_to_content_scale_x_ = dest_to_content_scale *
        content_size_floor.width() / content_size_ceil.width();
  if (content_size_floor.height() != content_size_ceil.height())
    dest_to_content_scale_y_ = dest_to_content_scale *
        content_size_floor.height() /content_size_ceil.height();

  gfx::Rect content_rect =
      gfx::ToEnclosingRect(gfx::ScaleRect(dest_rect_,
                                          dest_to_content_scale_x_,
                                          dest_to_content_scale_y_));
  // IndexFromSrcCoord clamps to valid tile ranges, so it's necessary to
  // check for non-intersection first.
  content_rect.Intersect(gfx::Rect(tiling_->tiling_data_.total_size()));
  if (content_rect.IsEmpty())
    return;

  left_ = tiling_->tiling_data_.TileXIndexFromSrcCoord(content_rect.x());
  top_ = tiling_->tiling_data_.TileYIndexFromSrcCoord(content_rect.y());
  right_ = tiling_->tiling_data_.TileXIndexFromSrcCoord(
      content_rect.right() - 1);
  bottom_ = tiling_->tiling_data_.TileYIndexFromSrcCoord(
      content_rect.bottom() - 1);

  tile_i_ = left_ - 1;
  tile_j_ = top_;
  ++(*this);
}

PictureLayerTiling::Iterator::~Iterator() {
}

PictureLayerTiling::Iterator& PictureLayerTiling::Iterator::operator++() {
  if (tile_j_ > bottom_)
    return *this;

  bool first_time = tile_i_ < left_;
  bool new_row = false;
  tile_i_++;
  if (tile_i_ > right_) {
    tile_i_ = left_;
    tile_j_++;
    new_row = true;
    if (tile_j_ > bottom_) {
      current_tile_ = NULL;
      return *this;
    }
  }

  current_tile_ = tiling_->TileAt(tile_i_, tile_j_);

  // Calculate the current geometry rect.  Due to floating point rounding
  // and ToEnclosingRect, tiles might overlap in destination space on the
  // edges.
  gfx::Rect last_geometry_rect = current_geometry_rect_;

  gfx::Rect content_rect = tiling_->tiling_data_.TileBounds(tile_i_, tile_j_);

  current_geometry_rect_ = gfx::ToEnclosingRect(
      gfx::ScaleRect(content_rect, 1 / dest_to_content_scale_x_,
                                   1 / dest_to_content_scale_y_));

  current_geometry_rect_.Intersect(dest_rect_);

  if (first_time)
    return *this;

  // Iteration happens left->right, top->bottom.  Running off the bottom-right
  // edge is handled by the intersection above with dest_rect_.  Here we make
  // sure that the new current geometry rect doesn't overlap with the last.
  int min_left;
  int min_top;
  if (new_row) {
    min_left = dest_rect_.x();
    min_top = last_geometry_rect.bottom();
  } else {
    min_left = last_geometry_rect.right();
    min_top = last_geometry_rect.y();
  }

  int inset_left = std::max(0, min_left - current_geometry_rect_.x());
  int inset_top = std::max(0, min_top - current_geometry_rect_.y());
  current_geometry_rect_.Inset(inset_left, inset_top, 0, 0);

  if (!new_row) {
    DCHECK_EQ(last_geometry_rect.right(), current_geometry_rect_.x());
    DCHECK_EQ(last_geometry_rect.bottom(), current_geometry_rect_.bottom());
    DCHECK_EQ(last_geometry_rect.y(), current_geometry_rect_.y());
  }

  return *this;
}

gfx::Rect PictureLayerTiling::Iterator::geometry_rect() const {
  return current_geometry_rect_;
}

gfx::RectF PictureLayerTiling::Iterator::texture_rect() const {
  gfx::PointF tex_origin =
      tiling_->tiling_data_.TileBoundsWithBorder(tile_i_, tile_j_).origin();

  // Convert from dest space => content space => texture space.
  gfx::RectF texture_rect(current_geometry_rect_);
  texture_rect.Scale(dest_to_content_scale_x_,
                     dest_to_content_scale_y_);
  texture_rect.Offset(-tex_origin.OffsetFromOrigin());

  DCHECK_GE(texture_rect.x(), 0);
  DCHECK_GE(texture_rect.y(), 0);
  DCHECK_LE(texture_rect.right(), texture_size().width());
  DCHECK_LE(texture_rect.bottom(), texture_size().height());

  return texture_rect;
}

gfx::Size PictureLayerTiling::Iterator::texture_size() const {
  return tiling_->tiling_data_.max_texture_size();
}

void PictureLayerTiling::UpdateTilePriorities(
    WhichTree tree,
    const gfx::Size& device_viewport,
    const gfx::RectF viewport_in_layer_space,
    float last_layer_contents_scale,
    float current_layer_contents_scale,
    const gfx::Transform& last_screen_transform,
    const gfx::Transform& current_screen_transform,
    double time_delta) {
  TRACE_EVENT0("cc", "PictureLayerTiling::UpdateTilePriorities");
  gfx::Rect content_rect = ContentRect();
  if (content_rect.IsEmpty())
    return;

  gfx::Rect viewport_in_content_space =
      gfx::ToEnclosingRect(gfx::ScaleRect(viewport_in_layer_space,
                                          contents_scale_));
  gfx::Rect inflated_rect = viewport_in_content_space;
  inflated_rect.Inset(
      -TilePriority::kMaxDistanceInContentSpace,
      -TilePriority::kMaxDistanceInContentSpace,
      -TilePriority::kMaxDistanceInContentSpace,
      -TilePriority::kMaxDistanceInContentSpace);
  inflated_rect.Intersect(ContentRect());

  // Iterate through all of the tiles that were live last frame but will
  // not be live this frame, and mark them as being dead.
  for (TilingData::DifferenceIterator iter(&tiling_data_,
                                           last_prioritized_rect_,
                                           inflated_rect);
       iter;
       ++iter) {
    TileMap::iterator find = tiles_.find(iter.index());
    if (find == tiles_.end())
      continue;

    TilePriority priority;
    DCHECK(!priority.is_live);
    Tile* tile = find->second.get();
    tile->set_priority(tree, priority);
  }
  last_prioritized_rect_ = inflated_rect;

  gfx::Rect view_rect(device_viewport);
  float current_scale = current_layer_contents_scale / contents_scale_;
  float last_scale = last_layer_contents_scale / contents_scale_;

  // Fast path tile priority calculation when both transforms are translations.
  if (last_screen_transform.IsIdentityOrTranslation() &&
      current_screen_transform.IsIdentityOrTranslation())
  {
    gfx::Vector2dF current_offset(
        current_screen_transform.matrix().get(0, 3),
        current_screen_transform.matrix().get(1, 3));
    gfx::Vector2dF last_offset(
        last_screen_transform.matrix().get(0, 3),
        last_screen_transform.matrix().get(1, 3));

    for (TilingData::Iterator iter(&tiling_data_, inflated_rect);
         iter; ++iter) {
      TileMap::iterator find = tiles_.find(iter.index());
      if (find == tiles_.end())
        continue;
      Tile* tile = find->second.get();

      gfx::Rect tile_bounds =
          tiling_data_.TileBounds(iter.index_x(), iter.index_y());
      gfx::RectF current_screen_rect = gfx::ScaleRect(
          tile_bounds,
          current_scale,
          current_scale) + current_offset;
      gfx::RectF last_screen_rect = gfx::ScaleRect(
          tile_bounds,
          last_scale,
          last_scale) + last_offset;

      float distance_to_visible_in_pixels =
          TilePriority::manhattanDistance(current_screen_rect, view_rect);

      float time_to_visible_in_seconds =
          TilePriority::TimeForBoundsToIntersect(
              last_screen_rect, current_screen_rect, time_delta, view_rect);
      TilePriority priority(
          resolution_,
          time_to_visible_in_seconds,
          distance_to_visible_in_pixels);
      tile->set_priority(tree, priority);
    }
  }
  else
  {
    for (TilingData::Iterator iter(&tiling_data_, inflated_rect);
         iter; ++iter) {
      TileMap::iterator find = tiles_.find(iter.index());
      if (find == tiles_.end())
        continue;
      Tile* tile = find->second.get();

      gfx::Rect tile_bounds =
          tiling_data_.TileBounds(iter.index_x(), iter.index_y());
      gfx::RectF current_layer_content_rect = gfx::ScaleRect(
          tile_bounds,
          current_scale,
          current_scale);
      gfx::RectF current_screen_rect = MathUtil::mapClippedRect(
          current_screen_transform, current_layer_content_rect);
      gfx::RectF last_layer_content_rect = gfx::ScaleRect(
          tile_bounds,
          last_scale,
          last_scale);
      gfx::RectF last_screen_rect  = MathUtil::mapClippedRect(
          last_screen_transform, last_layer_content_rect);

      float distance_to_visible_in_pixels =
          TilePriority::manhattanDistance(current_screen_rect, view_rect);

      float time_to_visible_in_seconds =
          TilePriority::TimeForBoundsToIntersect(
              last_screen_rect, current_screen_rect, time_delta, view_rect);

      TilePriority priority(
          resolution_,
          time_to_visible_in_seconds,
          distance_to_visible_in_pixels);
      tile->set_priority(tree, priority);
    }
  }
}

void PictureLayerTiling::DidBecomeActive() {
  for (TileMap::const_iterator it = tiles_.begin(); it != tiles_.end(); ++it) {
    it->second->set_priority(ACTIVE_TREE, it->second->priority(PENDING_TREE));
    it->second->set_priority(PENDING_TREE, TilePriority());

    // Tile holds a ref onto a picture pile. If the tile never gets invalidated
    // and recreated, then that picture pile ref could exist indefinitely.  To
    // prevent this, ask the client to update the pile to its own ref.  This
    // will cause PicturePileImpls and their clones to get deleted once the
    // corresponding PictureLayerImpl and any in flight raster jobs go out of
    // scope.
    client_->UpdatePile(it->second);
  }
}

}  // namespace cc
