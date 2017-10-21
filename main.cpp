#include <iostream>
#include <chrono>
#include <cfloat>
#include <cassert>

// linux
#include <dirent.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "log.h"
#include "defines.h"
#include "direction.h"
#include "bitmap.h"
#include "conversion.h"
#include "rect.h"
#include "tile.h"
#include "element.h"
#include "object_array.h"
#include "interactive.h"
#include "quad_tree.h"

#define PLAYER_SPEED 5.5f
#define PLAYER_WALK_DELAY 0.15f
#define PLAYER_IDLE_SPEED 0.0025f
#define PLAYER_BOW_DRAW_DELAY 0.3f

struct Player_t{
     Position_t pos;
     Vec_t accel;
     Vec_t vel;
     Direction_t face;
     F32 radius;
     F32 push_time;
     S8 walk_frame;
     S8 walk_frame_delta;
     F32 walk_frame_time;
     bool has_bow;
     F32 bow_draw_time;
};

void player_spawn(Player_t* player, Coord_t coord){
     *player = {};
     player->walk_frame_delta = 1;
     player->radius = 3.5f / 272.0f;
     player->pos = coord_to_pos_at_tile_center(coord);
     player->has_bow = true;
}

Interactive_t* quad_tree_interactive_find_at(QuadTreeNode_t<Interactive_t>* root, Coord_t coord){
     return quad_tree_find_at(root, coord.x, coord.y);
}

Interactive_t* quad_tree_interactive_solid_at(QuadTreeNode_t<Interactive_t>* root, Coord_t coord){
     Interactive_t* interactive = quad_tree_find_at(root, coord.x, coord.y);
     if(interactive && interactive_is_solid(interactive)){
          return interactive;
     }

     return nullptr;
}

struct Block_t{
     Position_t pos;
     Vec_t accel;
     Vec_t vel;
     DirectionMask_t force;
     Direction_t face;
     Element_t element;
     Pixel_t push_start;
     F32 fall_time;
};

S16 get_object_x(Block_t* block){
     return block->pos.pixel.x + HALF_TILE_SIZE_IN_PIXELS;
}

S16 get_object_y(Block_t* block){
     return block->pos.pixel.y + HALF_TILE_SIZE_IN_PIXELS;
}

Pixel_t block_center_pixel(Block_t* block){
     return block->pos.pixel + Pixel_t{HALF_TILE_SIZE_IN_PIXELS, HALF_TILE_SIZE_IN_PIXELS};
}

Coord_t block_get_coord(Block_t* block){
     Pixel_t center = block->pos.pixel + Pixel_t{HALF_TILE_SIZE_IN_PIXELS, HALF_TILE_SIZE_IN_PIXELS};
     return pixel_to_coord(center);
}

bool block_on_ice(Block_t* block, TileMap_t* tilemap, QuadTreeNode_t<Interactive_t>* interactive_quad_tree){
     if(block->pos.z == 0){
          Coord_t coord = block_get_coord(block);
          Interactive_t* interactive = quad_tree_interactive_find_at(interactive_quad_tree, coord);
          if(interactive){
               if(interactive->type == INTERACTIVE_TYPE_POPUP){
                    if(interactive->popup.lift.ticks == 1 && interactive->popup.iced){
                         return true;
                    }
               }
          }
          return tilemap_is_iced(tilemap, coord);
     }

     // TODO: check for blocks below
     return false;
}

bool blocks_at_collidable_height(Block_t* a, Block_t* b){
     S8 a_top = a->pos.z + HEIGHT_INTERVAL - 1;
     S8 b_top = b->pos.z + HEIGHT_INTERVAL - 1;

     if(a_top >= b->pos.z && a_top <= b_top){
          return true;
     }

     if(a->pos.z >= b->pos.z && a->pos.z <= b_top){
          return true;
     }

     return false;
}

#define BLOCK_QUAD_TREE_MAX_QUERY 16

Block_t* block_against_another_block(Block_t* block_to_check, Direction_t direction, QuadTreeNode_t<Block_t>* block_quad_tree){
     Pixel_t center = block_center_pixel(block_to_check);
     Rect_t rect = {};
     rect.left = center.x - (2 * TILE_SIZE_IN_PIXELS);
     rect.right = center.x + (2 * TILE_SIZE_IN_PIXELS);
     rect.bottom = center.y - (2 * TILE_SIZE_IN_PIXELS);
     rect.top = center.y + (2 * TILE_SIZE_IN_PIXELS);

     S16 block_count = 0;
     Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
     quad_tree_find_in(block_quad_tree, rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

     switch(direction){
     default:
          break;
     case DIRECTION_LEFT:
          for(S16 i = 0; i < block_count; i++){
               Block_t* block = blocks[i];
               if(!blocks_at_collidable_height(block_to_check, block)){
                    continue;
               }

               if((block->pos.pixel.x + TILE_SIZE_IN_PIXELS) == block_to_check->pos.pixel.x &&
                  block->pos.pixel.y >= block_to_check->pos.pixel.y &&
                  block->pos.pixel.y < (block_to_check->pos.pixel.y + TILE_SIZE_IN_PIXELS)){
                    return block;
               }
          }
          break;
     case DIRECTION_RIGHT:
          for(S16 i = 0; i < block_count; i++){
               Block_t* block = blocks[i];
               if(!blocks_at_collidable_height(block_to_check, block)){
                    continue;
               }

               if(block->pos.pixel.x == (block_to_check->pos.pixel.x + TILE_SIZE_IN_PIXELS) &&
                  block->pos.pixel.y >= block_to_check->pos.pixel.y &&
                  block->pos.pixel.y < (block_to_check->pos.pixel.y + TILE_SIZE_IN_PIXELS)){
                    return block;
               }
          }
          break;
     case DIRECTION_DOWN:
          for(S16 i = 0; i < block_count; i++){
               Block_t* block = blocks[i];
               if(!blocks_at_collidable_height(block_to_check, block)){
                    continue;
               }

               if((block->pos.pixel.y + TILE_SIZE_IN_PIXELS) == block_to_check->pos.pixel.y &&
                  block->pos.pixel.x >= block_to_check->pos.pixel.x &&
                  block->pos.pixel.x < (block_to_check->pos.pixel.x + TILE_SIZE_IN_PIXELS)){
                    return block;
               }
          }
          break;
     case DIRECTION_UP:
          for(S16 i = 0; i < block_count; i++){
               Block_t* block = blocks[i];
               if(!blocks_at_collidable_height(block_to_check, block)){
                    continue;
               }

               if(block->pos.pixel.y == (block_to_check->pos.pixel.y + TILE_SIZE_IN_PIXELS) &&
                  block->pos.pixel.x >= block_to_check->pos.pixel.x &&
                  block->pos.pixel.x < (block_to_check->pos.pixel.x + TILE_SIZE_IN_PIXELS)){
                    return block;
               }
          }
          break;
     }

     return nullptr;
}

Block_t* block_inside_another_block(Block_t* block_to_check, QuadTreeNode_t<Block_t>* block_quad_tree){
     // TODO: need more complicated function to detect this
     Rect_t rect = {block_to_check->pos.pixel.x, block_to_check->pos.pixel.y,
                    (S16)(block_to_check->pos.pixel.x + TILE_SIZE_IN_PIXELS - 1),
                    (S16)(block_to_check->pos.pixel.y + TILE_SIZE_IN_PIXELS - 1)};
     Pixel_t center = block_center_pixel(block_to_check);
     Rect_t surrounding_rect = {(S16)(center.x - (2 * TILE_SIZE_IN_PIXELS)),
                                (S16)(center.y - (2 * TILE_SIZE_IN_PIXELS)),
                                (S16)(center.x + (2 * TILE_SIZE_IN_PIXELS)),
                                (S16)(center.y + (2 * TILE_SIZE_IN_PIXELS))};
     S16 block_count = 0;
     Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
     quad_tree_find_in(block_quad_tree, surrounding_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);
     for(S16 i = 0; i < block_count; i++){
          if(blocks[i] == block_to_check) continue;
          Block_t* block = blocks[i];

          Pixel_t top_left {block->pos.pixel.x, (S16)(block->pos.pixel.y + TILE_SIZE_IN_PIXELS - 1)};
          Pixel_t top_right {(S16)(block->pos.pixel.x + TILE_SIZE_IN_PIXELS - 1), (S16)(block->pos.pixel.y + TILE_SIZE_IN_PIXELS - 1)};
          Pixel_t bottom_right {(S16)(block->pos.pixel.x + TILE_SIZE_IN_PIXELS - 1), block->pos.pixel.y};

          if(pixel_in_rect(block->pos.pixel, rect) ||
             pixel_in_rect(top_left, rect) ||
             pixel_in_rect(top_right, rect) ||
             pixel_in_rect(bottom_right, rect)){
               return block;
          }
     }

     return nullptr;
}

Block_t* block_held_up_by_another_block(Block_t* block_to_check, QuadTreeNode_t<Block_t>* block_quad_tree){
     // TODO: need more complicated function to detect this
     Rect_t rect = {block_to_check->pos.pixel.x, block_to_check->pos.pixel.y,
                    (S16)(block_to_check->pos.pixel.x + TILE_SIZE_IN_PIXELS - 1),
                    (S16)(block_to_check->pos.pixel.y + TILE_SIZE_IN_PIXELS - 1)};
     Pixel_t center = block_center_pixel(block_to_check);
     Rect_t surrounding_rect = {(S16)(center.x - (2 * TILE_SIZE_IN_PIXELS)),
                                (S16)(center.y - (2 * TILE_SIZE_IN_PIXELS)),
                                (S16)(center.x + (2 * TILE_SIZE_IN_PIXELS)),
                                (S16)(center.y + (2 * TILE_SIZE_IN_PIXELS))};
     S16 block_count = 0;
     Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
     quad_tree_find_in(block_quad_tree, surrounding_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);
     S8 held_at_height = block_to_check->pos.z - HEIGHT_INTERVAL;
     for(S16 i = 0; i < block_count; i++){
          Block_t* block = blocks[i];
          if(block == block_to_check || block->pos.z != held_at_height) continue;

          Pixel_t top_left {block->pos.pixel.x, (S16)(block->pos.pixel.y + TILE_SIZE_IN_PIXELS - 1)};
          Pixel_t top_right {(S16)(block->pos.pixel.x + TILE_SIZE_IN_PIXELS - 1), (S16)(block->pos.pixel.y + TILE_SIZE_IN_PIXELS - 1)};
          Pixel_t bottom_right {(S16)(block->pos.pixel.x + TILE_SIZE_IN_PIXELS - 1), block->pos.pixel.y};

          if(pixel_in_rect(block->pos.pixel, rect) ||
             pixel_in_rect(top_left, rect) ||
             pixel_in_rect(top_right, rect) ||
             pixel_in_rect(bottom_right, rect)){
               return block;
          }
     }

     return nullptr;
}

bool block_adjacent_pixels_to_check(Block_t* block_to_check, Direction_t direction, Pixel_t* a, Pixel_t* b){
     switch(direction){
     default:
          break;
     case DIRECTION_LEFT:
     {
          // check bottom corner
          Pixel_t pixel = block_to_check->pos.pixel;
          pixel.x--;
          *a = pixel;

          // top corner
          pixel.y += (TILE_SIZE_IN_PIXELS - 1);
          *b = pixel;
          return true;
     };
     case DIRECTION_RIGHT:
     {
          // check bottom corner
          Pixel_t pixel = block_to_check->pos.pixel;
          pixel.x += TILE_SIZE_IN_PIXELS;
          *a = pixel;

          // check top corner
          pixel.y += (TILE_SIZE_IN_PIXELS - 1);
          *b = pixel;
          return true;
     };
     case DIRECTION_DOWN:
     {
          // check left corner
          Pixel_t pixel = block_to_check->pos.pixel;
          pixel.y--;
          *a = pixel;

          // check right corner
          pixel.x += (TILE_SIZE_IN_PIXELS - 1);
          *b = pixel;
          return true;
     };
     case DIRECTION_UP:
     {
          // check left corner
          Pixel_t pixel = block_to_check->pos.pixel;
          pixel.y += TILE_SIZE_IN_PIXELS;
          *a = pixel;

          // check right corner
          pixel.x += (TILE_SIZE_IN_PIXELS - 1);
          *b = pixel;
          return true;
     };
     }

     return false;
}

Tile_t* block_against_solid_tile(Block_t* block_to_check, Direction_t direction, TileMap_t* tilemap){
     Pixel_t pixel_a;
     Pixel_t pixel_b;

     if(!block_adjacent_pixels_to_check(block_to_check, direction, &pixel_a, &pixel_b)){
          return nullptr;
     }

     Coord_t tile_coord = pixel_to_coord(pixel_a);
     Tile_t* tile = tilemap_get_tile(tilemap, tile_coord);
     if(tile && tile->id) return tile;

     tile_coord = pixel_to_coord(pixel_b);
     tile = tilemap_get_tile(tilemap, tile_coord);
     if(tile && tile->id) return tile;

     return nullptr;
}

#define ARROW_DISINTEGRATE_DELAY 4.0f
#define ARROW_SHOOT_HEIGHT 7
#define ARROW_FALL_DELAY 2.0f

struct Arrow_t{
     Position_t pos;
     Direction_t face;
     Element_t element;
     F32 vel;
     S16 element_from_block;

     bool alive;
     F32 stuck_time; // TODO: track objects we are stuck in
     F32 fall_time;
};

#define ARROW_ARRAY_MAX 32

struct ArrowArray_t{
     Arrow_t arrows[ARROW_ARRAY_MAX];
};

bool init(ArrowArray_t* arrow_array){
     for(S16 i = 0; i < ARROW_ARRAY_MAX; i++){
          arrow_array->arrows[i].alive = false;
     }

     return true;
}

Arrow_t* arrow_array_spawn(ArrowArray_t* arrow_array, Position_t pos, Direction_t face){
     for(S16 i = 0; i < ARROW_ARRAY_MAX; i++){
          if(!arrow_array->arrows[i].alive){
               arrow_array->arrows[i].pos = pos;
               arrow_array->arrows[i].face = face;
               arrow_array->arrows[i].alive = true;
               arrow_array->arrows[i].stuck_time = 0.0f;
               arrow_array->arrows[i].element = ELEMENT_NONE;
               arrow_array->arrows[i].vel = 1.25f;
               return arrow_array->arrows + i;
          }
     }

     return nullptr;
}

Vec_t collide_circle_with_line(Vec_t circle_center, F32 circle_radius, Vec_t a, Vec_t b, bool* collided){
     // move data we care about to the origin
     Vec_t c = circle_center - a;
     Vec_t l = b - a;
     Vec_t closest_point_on_l_to_c = vec_project_onto(c, l);
     Vec_t collision_to_c = closest_point_on_l_to_c - c;

     if(vec_magnitude(collision_to_c) < circle_radius){
          // find edge of circle in that direction
          Vec_t edge_of_circle = vec_normalize(collision_to_c) * circle_radius;
          *collided = true;
          return collision_to_c - edge_of_circle;
     }

     return vec_zero();
}

S16 range_passes_tile_boundary(S16 a, S16 b, S16 ignore){
     if(a == b) return 0;
     if(a > b){
          if((b % TILE_SIZE_IN_PIXELS) == 0) return 0;
          SWAP(a, b);
     }

     for(S16 i = a; i <= b; i++){
          if((i % TILE_SIZE_IN_PIXELS) == 0 && i != ignore){
               return i;
          }
     }

     return 0;
}

GLuint create_texture_from_bitmap(AlphaBitmap_t* bitmap){
     GLuint texture = 0;

     glGenTextures(1, &texture);

     glBindTexture(GL_TEXTURE_2D, texture);

     glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
     glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
     glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
     glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bitmap->width, bitmap->height, 0,  GL_RGBA, GL_UNSIGNED_BYTE, bitmap->pixels);

     return texture;
}

GLuint transparent_texture_from_file(const char* filepath){
     Bitmap_t bitmap = bitmap_load_from_file(filepath);
     if(bitmap.raw.byte_count == 0) return 0;
     AlphaBitmap_t alpha_bitmap = bitmap_to_alpha_bitmap(&bitmap, BitmapPixel_t{255, 0, 255});
     free(bitmap.raw.bytes);
     GLuint texture_id = create_texture_from_bitmap(&alpha_bitmap);
     free(alpha_bitmap.pixels);
     return texture_id;
}

#define THEME_FRAMES_WIDE 16
#define THEME_FRAMES_TALL 32
#define THEME_FRAME_WIDTH 0.0625f
#define THEME_FRAME_HEIGHT 0.03125f

Vec_t theme_frame(S16 x, S16 y){
     y = (THEME_FRAMES_TALL - 1) - y;
     return Vec_t{(F32)(x) * THEME_FRAME_WIDTH, (F32)(y) * THEME_FRAME_HEIGHT};
}

#define ARROW_FRAME_WIDTH 0.25f
#define ARROW_FRAME_HEIGHT 0.0625f
#define ARROW_FRAMES_TALL 16
#define ARROW_FRAMES_WIDE 4

Vec_t arrow_frame(S8 x, S8 y) {
     y = (ARROW_FRAMES_TALL - 1) - y;
     return Vec_t{(F32)(x) * ARROW_FRAME_WIDTH, (F32)(y) * ARROW_FRAME_HEIGHT};
}

#define PLAYER_FRAME_WIDTH 0.25f
#define PLAYER_FRAME_HEIGHT 0.03125f
#define PLAYER_FRAMES_WIDE 4
#define PLAYER_FRAMES_TALL 32

// new quakelive bot settings
// bot_thinktime
// challenge mode

Vec_t player_frame(S8 x, S8 y){
     y = (PLAYER_FRAMES_TALL - 1) - y;
     return Vec_t{(F32)(x) * PLAYER_FRAME_WIDTH, (F32)(y) * PLAYER_FRAME_HEIGHT};
}

void toggle_flag(U16* flags, U16 flag){
     if(*flags & flag){
          *flags &= ~flag;
     }else{
          *flags |= flag;
     }
}

void toggle_electricity(TileMap_t* tilemap, QuadTreeNode_t<Interactive_t>* interactive_quad_tree, Coord_t coord,
                        Direction_t direction, bool activated_by_door){
     Coord_t adjacent_coord = coord + direction;
     Tile_t* tile = tilemap_get_tile(tilemap, adjacent_coord);
     if(!tile) return;

     Interactive_t* interactive = quad_tree_interactive_find_at(interactive_quad_tree, adjacent_coord);
     if(interactive){
          switch(interactive->type){
          default:
               break;
          case INTERACTIVE_TYPE_POPUP:
          {
               interactive->popup.lift.up = !interactive->popup.lift.up;
               if(tile->flags & TILE_FLAG_ICED){
                    tile->flags &= ~TILE_FLAG_ICED;
               }
          } break;
          case INTERACTIVE_TYPE_DOOR:
               interactive->door.lift.up = !interactive->door.lift.up;
               if(!activated_by_door) toggle_electricity(tilemap, interactive_quad_tree,
                                                         coord_move(coord, interactive->door.face, 3),
                                                         interactive->door.face, true);
               break;
          }
     }

     if(tile->flags & (TILE_FLAG_WIRE_LEFT | TILE_FLAG_WIRE_UP | TILE_FLAG_WIRE_RIGHT | TILE_FLAG_WIRE_DOWN)){
          switch(direction){
          default:
               return;
          case DIRECTION_LEFT:
               if(!(tile->flags & TILE_FLAG_WIRE_RIGHT)){
                    return;
               }
               break;
          case DIRECTION_RIGHT:
               if(!(tile->flags & TILE_FLAG_WIRE_LEFT)){
                    return;
               }
               break;
          case DIRECTION_UP:
               if(!(tile->flags & TILE_FLAG_WIRE_DOWN)){
                    return;
               }
               break;
          case DIRECTION_DOWN:
               if(!(tile->flags & TILE_FLAG_WIRE_UP)){
                    return;
               }
               break;
          }

          // toggle wire state
          if(tile->flags & TILE_FLAG_WIRE_STATE){
               tile->flags &= ~TILE_FLAG_WIRE_STATE;
          }else{
               tile->flags |= TILE_FLAG_WIRE_STATE;
          }

          if(tile->flags & TILE_FLAG_WIRE_LEFT && direction != DIRECTION_RIGHT){
               toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_LEFT, false);
          }

          if(tile->flags & TILE_FLAG_WIRE_RIGHT && direction != DIRECTION_LEFT){
               toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_RIGHT, false);
          }

          if(tile->flags & TILE_FLAG_WIRE_DOWN && direction != DIRECTION_UP){
               toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_DOWN, false);
          }

          if(tile->flags & TILE_FLAG_WIRE_UP && direction != DIRECTION_DOWN){
               toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_UP, false);
          }
     }else if(tile->flags & (TILE_FLAG_WIRE_CLUSTER_LEFT | TILE_FLAG_WIRE_CLUSTER_MID | TILE_FLAG_WIRE_CLUSTER_RIGHT)){
          bool all_on_before = tile_flags_cluster_all_on(tile->flags);

          Direction_t cluster_direction = tile_flags_cluster_direction(tile->flags);
          switch(cluster_direction){
          default:
               break;
          case DIRECTION_LEFT:
               switch(direction){
               default:
                    break;
               case DIRECTION_LEFT:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_MID) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_MID_ON);
                    break;
               case DIRECTION_UP:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_LEFT) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_LEFT_ON);
                    break;
               case DIRECTION_DOWN:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_RIGHT) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_RIGHT_ON);
                    break;
               }
               break;
          case DIRECTION_RIGHT:
               switch(direction){
               default:
                    break;
               case DIRECTION_RIGHT:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_MID) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_MID_ON);
                    break;
               case DIRECTION_DOWN:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_LEFT) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_LEFT_ON);
                    break;
               case DIRECTION_UP:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_RIGHT) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_RIGHT_ON);
                    break;
               }
               break;
          case DIRECTION_DOWN:
               switch(direction){
               default:
                    break;
               case DIRECTION_DOWN:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_MID) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_MID_ON);
                    break;
               case DIRECTION_LEFT:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_LEFT) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_LEFT_ON);
                    break;
               case DIRECTION_RIGHT:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_RIGHT) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_RIGHT_ON);
                    break;
               }
               break;
          case DIRECTION_UP:
               switch(direction){
               default:
                    break;
               case DIRECTION_UP:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_MID) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_MID_ON);
                    break;
               case DIRECTION_RIGHT:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_LEFT) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_LEFT_ON);
                    break;
               case DIRECTION_LEFT:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_RIGHT) toggle_flag(&tile->flags, TILE_FLAG_WIRE_CLUSTER_RIGHT_ON);
                    break;
               }
               break;
          }

          bool all_on_after = tile_flags_cluster_all_on(tile->flags);

          if(all_on_before != all_on_after){
               toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, cluster_direction, false);
          }
     }
}

void activate(TileMap_t* tilemap, QuadTreeNode_t<Interactive_t>* interactive_quad_tree, Coord_t coord){
     Interactive_t* interactive = quad_tree_interactive_find_at(interactive_quad_tree, coord);
     if(!interactive) return;

     if(interactive->type != INTERACTIVE_TYPE_LEVER &&
        interactive->type != INTERACTIVE_TYPE_PRESSURE_PLATE &&
        interactive->type != INTERACTIVE_TYPE_LIGHT_DETECTOR &&
        interactive->type != INTERACTIVE_TYPE_ICE_DETECTOR) return;

     toggle_electricity(tilemap, interactive_quad_tree, coord, DIRECTION_LEFT, false);
     toggle_electricity(tilemap, interactive_quad_tree, coord, DIRECTION_RIGHT, false);
     toggle_electricity(tilemap, interactive_quad_tree, coord, DIRECTION_UP, false);
     toggle_electricity(tilemap, interactive_quad_tree, coord, DIRECTION_DOWN, false);
}

Interactive_t* block_against_solid_interactive(Block_t* block_to_check, Direction_t direction,
                                               QuadTreeNode_t<Interactive_t>* interactive_quad_tree){
     Pixel_t pixel_a;
     Pixel_t pixel_b;

     if(!block_adjacent_pixels_to_check(block_to_check, direction, &pixel_a, &pixel_b)){
          return nullptr;
     }

     Coord_t tile_coord = pixel_to_coord(pixel_a);
     Interactive_t* interactive = quad_tree_interactive_solid_at(interactive_quad_tree, tile_coord);
     if(interactive) return interactive;

     tile_coord = pixel_to_coord(pixel_b);
     interactive = quad_tree_interactive_solid_at(interactive_quad_tree, tile_coord);
     if(interactive) return interactive;

     return nullptr;
}

void block_push(Block_t* block, Direction_t direction, TileMap_t* tilemap, QuadTreeNode_t<Interactive_t>* interactive_quad_tree,
                QuadTreeNode_t<Block_t>* block_quad_tree, bool pushed_by_player){
     Block_t* against_block = block_against_another_block(block, direction, block_quad_tree);
     if(against_block){
          if(!pushed_by_player && block_on_ice(against_block, tilemap, interactive_quad_tree)){
               block_push(against_block, direction, tilemap, interactive_quad_tree, block_quad_tree, false);
          }

          return;
     }

     if(block_against_solid_tile(block, direction, tilemap)) return;
     if(block_against_solid_interactive(block, direction, interactive_quad_tree)) return;

     switch(direction){
     default:
          break;
     case DIRECTION_LEFT:
          block->accel.x = -PLAYER_SPEED * 0.99f;
          break;
     case DIRECTION_RIGHT:
          block->accel.x = PLAYER_SPEED * 0.99f;
          break;
     case DIRECTION_DOWN:
          block->accel.y = -PLAYER_SPEED * 0.99f;
          break;
     case DIRECTION_UP:
          block->accel.y = PLAYER_SPEED * 0.99f;
          break;
     }

     block->push_start = block->pos.pixel;
}

void player_collide_coord(Position_t player_pos, Coord_t coord, F32 player_radius, Vec_t* pos_delta, bool* collide_with_coord){
     Coord_t player_coord = pos_to_coord(player_pos);
     Position_t relative = coord_to_pos(coord) - player_pos;
     Vec_t bottom_left = pos_to_vec(relative);
     Vec_t top_left {bottom_left.x, bottom_left.y + TILE_SIZE};
     Vec_t top_right {bottom_left.x + TILE_SIZE, bottom_left.y + TILE_SIZE};
     Vec_t bottom_right {bottom_left.x + TILE_SIZE, bottom_left.y};

     DirectionMask_t mask = directions_between(coord, player_coord);

     // TODO: figure out slide when on tile boundaries
     switch((U8)(mask)){
     default:
          break;
     case DIRECTION_MASK_LEFT:
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, bottom_left, top_left, collide_with_coord);
          break;
     case DIRECTION_MASK_RIGHT:
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, bottom_right, top_right, collide_with_coord);
          break;
     case DIRECTION_MASK_UP:
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, top_left, top_right, collide_with_coord);
          break;
     case DIRECTION_MASK_DOWN:
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, bottom_left, bottom_right, collide_with_coord);
          break;
     case DIRECTION_MASK_LEFT | DIRECTION_MASK_UP:
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, bottom_left, top_left, collide_with_coord);
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, top_left, top_right, collide_with_coord);
          break;
     case DIRECTION_MASK_LEFT | DIRECTION_MASK_DOWN:
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, bottom_left, top_left, collide_with_coord);
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, bottom_left, bottom_right, collide_with_coord);
          break;
     case DIRECTION_MASK_RIGHT | DIRECTION_MASK_UP:
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, bottom_right, top_right, collide_with_coord);
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, top_left, top_right, collide_with_coord);
          break;
     case DIRECTION_MASK_RIGHT | DIRECTION_MASK_DOWN:
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, bottom_right, top_right, collide_with_coord);
          *pos_delta += collide_circle_with_line(*pos_delta, player_radius, bottom_left, bottom_right, collide_with_coord);
          break;
     }
}

#define LIGHT_MAX_LINE_LEN 8

void illuminate_line(Coord_t start, Coord_t end, U8 value, TileMap_t* tilemap, QuadTreeNode_t<Block_t>* block_quad_tree){
     Coord_t coords[LIGHT_MAX_LINE_LEN];
     S8 coord_count = 0;

     // determine line of points using a modified bresenham to be symmetrical
     {
          if(start.x == end.x){
               // build a simple vertical path
               for(S16 y = start.y; y <= end.y; ++y){
                    coords[coord_count] = Coord_t{start.x, y};
                    coord_count++;
               }
          }else{
               F64 error = 0.0;
               F64 dx = (F64)(end.x) - (F64)(start.x);
               F64 dy = (F64)(end.y) - (F64)(start.y);
               F64 derror = fabs(dy / dx);

               S16 step_x = (start.x < end.x) ? 1 : -1;
               S16 step_y = (end.y - start.y >= 0) ? 1 : -1;
               S16 end_step_x = end.x + step_x;
               S16 sy = start.y;

               for(S16 sx = start.x; sx != end_step_x; sx += step_x){
                    Coord_t coord {sx, sy};
                    coords[coord_count] = coord;
                    coord_count++;

                    error += derror;
                    while(error >= 0.5){
                         coord = {sx, sy};

                         // only add non-duplicate coords
                         if(coords[coord_count - 1] != coord){
                              coords[coord_count] = coord;
                              coord_count++;
                         }

                         sy += step_y;
                         error -= 1.0;
                    }
               }
          }
     }

     for(S8 i = 0; i < coord_count; ++i){
          Tile_t* tile = tilemap_get_tile(tilemap, coords[i]);
          if(!tile) continue;

          S16 diff_x = abs(coords[i].x - start.x);
          S16 diff_y = abs(coords[i].y - start.y);
          U8 distance = static_cast<U8>(sqrt(static_cast<F32>(diff_x * diff_x + diff_y * diff_y)));

          U8 new_value = value - (distance * LIGHT_DECAY);

#if 0
          if(tile->solid.type == SOLID_PORTAL && tile->solid.portal.on){
               ConnectedPortals_t connected_portals = {};
               find_connected_portals(tilemap, coords[i], &connected_portals);
               Coord_t end_offset = end - coords[i];
               Coord_t prev_offset = coords[i] - coords[i - 1];

               for(S32 p = 0; p < connected_portals.coord_count; ++p){
                    if(connected_portals.coords[p] != coords[i]){
                         Coord_t dst_coord = connected_portals.coords[p] + prev_offset;
                         illuminate_line(dst_coord, dst_coord + end_offset, new_value, tilemap, block_array);
                    }
               }
               break;
          }
#endif

          Block_t* block = nullptr;
          if(coords[i] != start){
               if(tile_is_solid(tile)){
                    break;
               }

               S16 px = coords[i].x * TILE_SIZE_IN_PIXELS;
               S16 py = coords[i].y * TILE_SIZE_IN_PIXELS;
               Rect_t coord_rect {px, py, (S16)(px + TILE_SIZE_IN_PIXELS), (S16)(py + TILE_SIZE_IN_PIXELS)};

               S16 block_count = 0;
               Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
               quad_tree_find_in(block_quad_tree, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

               for(S16 b = 0; b < block_count; b++){
                    if(block_get_coord(blocks[b]) == coords[i]){
                         block = blocks[b];
                         break;
                    }
               }
          }

          if(tile->light < new_value) tile->light = new_value;
          if(block) break;
     }
}

void illuminate(Coord_t coord, U8 value, TileMap_t* tilemap, QuadTreeNode_t<Block_t>* block_quad_tree){
     if(coord.x < 0 || coord.y < 0 || coord.x >= tilemap->width || coord.y >= tilemap->height) return;

     S16 radius = ((value - BASE_LIGHT) / LIGHT_DECAY) + 1;

     if(radius < 0) return;

     Coord_t delta {radius, radius};
     Coord_t min = coord - delta;
     Coord_t max = coord + delta;

     for(S16 j = min.y + 1; j < max.y; ++j) {
          // bottom of box
          illuminate_line(coord, Coord_t{min.x, j}, value, tilemap, block_quad_tree);

          // top of box
          illuminate_line(coord, Coord_t{max.x, j}, value, tilemap, block_quad_tree);
     }

     for(S16 i = min.x + 1; i < max.x; ++i) {
          // left of box
          illuminate_line(coord, Coord_t{i, min.y,}, value, tilemap, block_quad_tree);

          // right of box
          illuminate_line(coord, Coord_t{i, max.y,}, value, tilemap, block_quad_tree);
     }
}

void spread_ice(Coord_t center, S16 radius, TileMap_t* tilemap, QuadTreeNode_t<Interactive_t>* interactive_quad_tree,
                QuadTreeNode_t<Block_t>* block_quad_tree){
     Coord_t delta {radius, radius};
     Coord_t min = center - delta;
     Coord_t max = center + delta;

     for(S16 y = min.y; y <= max.y; ++y){
          for(S16 x = min.x; x <= max.x; ++x){
               Coord_t coord{x, y};
               Tile_t* tile = tilemap_get_tile(tilemap, coord);
               if(tile && !tile_is_solid(tile)){
                    S16 px = coord.x * TILE_SIZE_IN_PIXELS;
                    S16 py = coord.y * TILE_SIZE_IN_PIXELS;
                    Rect_t coord_rect {(S16)(px - HALF_TILE_SIZE_IN_PIXELS),
                                       (S16)(py - HALF_TILE_SIZE_IN_PIXELS),
                                       (S16)(px + TILE_SIZE_IN_PIXELS + HALF_TILE_SIZE_IN_PIXELS),
                                       (S16)(py + TILE_SIZE_IN_PIXELS + HALF_TILE_SIZE_IN_PIXELS)};

                    S16 block_count = 0;
                    Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
                    quad_tree_find_in(block_quad_tree, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

                    Block_t* block = nullptr;
                    for(S16 i = 0; i < block_count; i++){
                         if(block_get_coord(blocks[i]) == coord && blocks[i]->pos.z == 0){
                              block = blocks[i];
                              break;
                         }
                    }

                    if(block){
                         if(block->element == ELEMENT_NONE) block->element = ELEMENT_ONLY_ICED;
                    }else{
                         Interactive_t* interactive = quad_tree_find_at(interactive_quad_tree, coord.x, coord.y);
                         if(interactive){
                              if(interactive->type == INTERACTIVE_TYPE_POPUP){
                                   if(interactive->popup.lift.ticks == 1){
                                        interactive->popup.iced = false;
                                        tile->flags |= TILE_FLAG_ICED;
                                   }else{
                                        interactive->popup.iced = true;
                                   }
                              }else if(interactive->type == INTERACTIVE_TYPE_PRESSURE_PLATE){
                                   tile->flags |= TILE_FLAG_ICED;
                              }

                              if(interactive->type == INTERACTIVE_TYPE_ICE_DETECTOR ||
                                 interactive->type == INTERACTIVE_TYPE_LIGHT_DETECTOR){
                                   tile->flags |= TILE_FLAG_ICED;
                              }
                         }else{
                              tile->flags |= TILE_FLAG_ICED;
                         }
                    }
               }
          }
     }
}

void melt_ice(Coord_t center, S16 radius, TileMap_t* tilemap, QuadTreeNode_t<Interactive_t>* interactive_quad_tree,
              QuadTreeNode_t<Block_t>* block_quad_tree){
     Coord_t delta {radius, radius};
     Coord_t min = center - delta;
     Coord_t max = center + delta;

     for(S16 y = min.y; y <= max.y; ++y){
          for(S16 x = min.x; x <= max.x; ++x){
               Coord_t coord{x, y};
               Tile_t* tile = tilemap_get_tile(tilemap, coord);
               if(tile && !tile_is_solid(tile)){
                    S16 px = coord.x * TILE_SIZE_IN_PIXELS;
                    S16 py = coord.y * TILE_SIZE_IN_PIXELS;
                    Rect_t coord_rect {(S16)(px - HALF_TILE_SIZE_IN_PIXELS),
                                       (S16)(py - HALF_TILE_SIZE_IN_PIXELS),
                                       (S16)(px + TILE_SIZE_IN_PIXELS + HALF_TILE_SIZE_IN_PIXELS),
                                       (S16)(py + TILE_SIZE_IN_PIXELS + HALF_TILE_SIZE_IN_PIXELS)};

                    S16 block_count = 0;
                    Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
                    quad_tree_find_in(block_quad_tree, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

                    Block_t* block = nullptr;
                    for(S16 i = 0; i < block_count; i++){
                         if(block_get_coord(blocks[i]) == coord && blocks[i]->pos.z == 0){
                              block = blocks[i];
                              break;
                         }
                    }

                    if(block){
                         if(block->element == ELEMENT_ONLY_ICED) block->element = ELEMENT_NONE;
                    }else{
                         Interactive_t* interactive = quad_tree_find_at(interactive_quad_tree, coord.x, coord.y);
                         if(interactive){
                              if(interactive->type == INTERACTIVE_TYPE_POPUP){
                                   if(interactive->popup.lift.ticks == 1){
                                        tile->flags &= ~TILE_FLAG_ICED;
                                   }else{
                                        interactive->popup.iced = false;
                                   }
                              }else if(interactive->type == INTERACTIVE_TYPE_PRESSURE_PLATE){
                                   interactive->pressure_plate.iced_under = false;
                                   tile->flags &= ~TILE_FLAG_ICED;
                              }else if(interactive->type == INTERACTIVE_TYPE_ICE_DETECTOR ||
                                       interactive->type == INTERACTIVE_TYPE_LIGHT_DETECTOR){
                                   tile->flags &= ~TILE_FLAG_ICED;
                              }
                         }else{
                              tile->flags &= ~TILE_FLAG_ICED;
                         }
                    }
               }
          }
     }
}

enum PlayerActionType_t{
     PLAYER_ACTION_TYPE_MOVE_LEFT_START,
     PLAYER_ACTION_TYPE_MOVE_LEFT_STOP,
     PLAYER_ACTION_TYPE_MOVE_UP_START,
     PLAYER_ACTION_TYPE_MOVE_UP_STOP,
     PLAYER_ACTION_TYPE_MOVE_RIGHT_START,
     PLAYER_ACTION_TYPE_MOVE_RIGHT_STOP,
     PLAYER_ACTION_TYPE_MOVE_DOWN_START,
     PLAYER_ACTION_TYPE_MOVE_DOWN_STOP,
     PLAYER_ACTION_TYPE_ACTIVATE_START,
     PLAYER_ACTION_TYPE_ACTIVATE_STOP,
     PLAYER_ACTION_TYPE_SHOOT_START,
     PLAYER_ACTION_TYPE_SHOOT_STOP,
     PLAYER_ACTION_TYPE_END_DEMO,
};

struct PlayerAction_t{
     bool move_left;
     bool move_right;
     bool move_up;
     bool move_down;
     bool activate;
     bool last_activate;
     bool shoot;
     bool reface;
};

enum DemoMode_t{
     DEMO_MODE_NONE,
     DEMO_MODE_PLAY,
     DEMO_MODE_RECORD,
};

struct DemoEntry_t{
     S64 frame;
     PlayerActionType_t player_action_type;
};

void demo_entry_get(DemoEntry_t* demo_entry, FILE* file){
     size_t read_count = fread(demo_entry, sizeof(*demo_entry), 1, file);
     if(read_count != 1) demo_entry->frame = (S64)(-1);
}

void player_action_perform(PlayerAction_t* player_action, Player_t* player, PlayerActionType_t player_action_type,
                           DemoMode_t demo_mode, FILE* demo_file, S64 frame_count){
     switch(player_action_type){
     default:
          break;
     case PLAYER_ACTION_TYPE_MOVE_LEFT_START:
          player_action->move_left = true;
          player->face = DIRECTION_LEFT;
          break;
     case PLAYER_ACTION_TYPE_MOVE_LEFT_STOP:
          player_action->move_left = false;
          if(player->face == DIRECTION_LEFT) player_action->reface = DIRECTION_LEFT;
          break;
     case PLAYER_ACTION_TYPE_MOVE_UP_START:
          player_action->move_up = true;
          player->face = DIRECTION_UP;
          break;
     case PLAYER_ACTION_TYPE_MOVE_UP_STOP:
          player_action->move_up = false;
          if(player->face == DIRECTION_UP) player_action->reface = DIRECTION_UP;
          break;
     case PLAYER_ACTION_TYPE_MOVE_RIGHT_START:
          player_action->move_right = true;
          player->face = DIRECTION_RIGHT;
          break;
     case PLAYER_ACTION_TYPE_MOVE_RIGHT_STOP:
          player_action->move_right = false;
          if(player->face == DIRECTION_RIGHT) player_action->reface = DIRECTION_RIGHT;
          break;
     case PLAYER_ACTION_TYPE_MOVE_DOWN_START:
          player_action->move_down = true;
          player->face = DIRECTION_DOWN;
          break;
     case PLAYER_ACTION_TYPE_MOVE_DOWN_STOP:
          player_action->move_down = false;
          if(player->face == DIRECTION_DOWN) player_action->reface = DIRECTION_DOWN;
          break;
     case PLAYER_ACTION_TYPE_ACTIVATE_START:
          player_action->activate = true;
          break;
     case PLAYER_ACTION_TYPE_ACTIVATE_STOP:
          player_action->activate = false;
          break;
     case PLAYER_ACTION_TYPE_SHOOT_START:
          player_action->shoot = true;
          break;
     case PLAYER_ACTION_TYPE_SHOOT_STOP:
          player_action->shoot = false;
          break;
     }

     if(demo_mode == DEMO_MODE_RECORD){
          DemoEntry_t demo_entry {frame_count, player_action_type};
          fwrite(&demo_entry, sizeof(demo_entry), 1, demo_file);
     }
}

#define MAP_VERSION 1

#pragma pack(push, 1)
struct MapTileV1_t{
     U8 id;
     U16 flags;
};

struct MapBlockV1_t{
     Pixel_t pixel;
     Direction_t face;
     Element_t element;
     S8 z;
};

struct MapPopupV1_t{
     bool up;
     bool iced;
};

struct MapDoorV1_t{
     bool up;
     Direction_t face;
};

struct MapInteractiveV1_t{
     InteractiveType_t type;
     Coord_t coord;

     union{
          PressurePlate_t pressure_plate;
          Detector_t detector;
          MapPopupV1_t popup;
          Stairs_t stairs;
          MapDoorV1_t door; // up or down
          Portal_t portal;
     };
};
#pragma pack(pop)

bool save_map_to_file(FILE* file, Coord_t player_start, const TileMap_t* tilemap, ObjectArray_t<Block_t>* block_array,
                      ObjectArray_t<Interactive_t>* interactive_array){
     // alloc and convert map elements to map format
     S32 map_tile_count = (S32)(tilemap->width) * (S32)(tilemap->height);
     MapTileV1_t* map_tiles = (MapTileV1_t*)(calloc(map_tile_count, sizeof(*map_tiles)));
     if(!map_tiles){
          LOG("%s(): failed to allocate %d tiles\n", __FUNCTION__, map_tile_count);
          return false;
     }

     MapBlockV1_t* map_blocks = (MapBlockV1_t*)(calloc(block_array->count, sizeof(*map_blocks)));
     if(!map_blocks){
          LOG("%s(): failed to allocate %d blocks\n", __FUNCTION__, block_array->count);
          return false;
     }

     MapInteractiveV1_t* map_interactives = (MapInteractiveV1_t*)(calloc(interactive_array->count, sizeof(*map_interactives)));
     if(!map_interactives){
          LOG("%s(): failed to allocate %d interactives\n", __FUNCTION__, interactive_array->count);
          return false;
     }

     // convert to map formats
     S32 index = 0;
     for(S32 y = 0; y < tilemap->height; y++){
          for(S32 x = 0; x < tilemap->width; x++){
               map_tiles[index].id = tilemap->tiles[y][x].id;
               map_tiles[index].flags = tilemap->tiles[y][x].flags;
               index++;
          }
     }

     for(S16 i = 0; i < block_array->count; i++){
          Block_t* block = block_array->elements + i;
          map_blocks[i].pixel = block->pos.pixel;
          map_blocks[i].z = block->pos.z;
          map_blocks[i].face = block->face;
          map_blocks[i].element = block->element;
     }

     for(S16 i = 0; i < interactive_array->count; i++){
          map_interactives[i].coord = interactive_array->elements[i].coord;
          map_interactives[i].type = interactive_array->elements[i].type;

          switch(map_interactives[i].type){
          default:
          case INTERACTIVE_TYPE_LEVER:
          case INTERACTIVE_TYPE_BOW:
               break;
          case INTERACTIVE_TYPE_PRESSURE_PLATE:
               map_interactives[i].pressure_plate = interactive_array->elements[i].pressure_plate;
               break;
          case INTERACTIVE_TYPE_LIGHT_DETECTOR:
          case INTERACTIVE_TYPE_ICE_DETECTOR:
               map_interactives[i].detector = interactive_array->elements[i].detector;
               break;
          case INTERACTIVE_TYPE_POPUP:
               map_interactives[i].popup.up = interactive_array->elements[i].popup.lift.up;
               map_interactives[i].popup.iced = interactive_array->elements[i].popup.iced;
               break;
          case INTERACTIVE_TYPE_DOOR:
               map_interactives[i].door.up = interactive_array->elements[i].door.lift.up;
               map_interactives[i].door.face = interactive_array->elements[i].door.face;
               break;
          case INTERACTIVE_TYPE_PORTAL:
               map_interactives[i].portal.face = interactive_array->elements[i].portal.face;
               map_interactives[i].portal.on = interactive_array->elements[i].portal.on;
               break;
          case INTERACTIVE_TYPE_STAIRS:
               map_interactives[i].stairs.up = interactive_array->elements[i].stairs.up;
               map_interactives[i].stairs.face = interactive_array->elements[i].stairs.face;
               break;
          case INTERACTIVE_TYPE_PROMPT:
               break;
          }
     }


     U8 map_version = MAP_VERSION;
     fwrite(&map_version, sizeof(map_version), 1, file);
     fwrite(&player_start, sizeof(player_start), 1, file);
     fwrite(&tilemap->width, sizeof(tilemap->width), 1, file);
     fwrite(&tilemap->height, sizeof(tilemap->height), 1, file);
     fwrite(&block_array->count, sizeof(block_array->count), 1, file);
     fwrite(&interactive_array->count, sizeof(interactive_array->count), 1, file);
     fwrite(map_tiles, sizeof(*map_tiles), map_tile_count, file);
     fwrite(map_blocks, sizeof(*map_blocks), block_array->count, file);
     fwrite(map_interactives, sizeof(*map_interactives), interactive_array->count, file);

     free(map_tiles);
     free(map_blocks);
     free(map_interactives);

     return true;
}

bool save_map(const char* filepath, Coord_t player_start, const TileMap_t* tilemap, ObjectArray_t<Block_t>* block_array,
              ObjectArray_t<Interactive_t>* interactive_array){
     // write to file
     FILE* f = fopen(filepath, "wb");
     if(!f){
          LOG("%s: fopen() failed\n", __FUNCTION__);
          return false;
     }
     bool success = save_map_to_file(f, player_start, tilemap, block_array, interactive_array);
     LOG("saved map %s\n", filepath);
     fclose(f);
     return success;
}

bool load_map_from_file(FILE* file, Coord_t* player_start, TileMap_t* tilemap, ObjectArray_t<Block_t>* block_array,
                        ObjectArray_t<Interactive_t>* interactive_array, const char* filepath){
     // read counts from file
     S16 map_width;
     S16 map_height;
     S16 interactive_count;
     S16 block_count;

     U8 map_version = MAP_VERSION;
     fread(&map_version, sizeof(map_version), 1, file);
     if(map_version != MAP_VERSION){
          LOG("%s(): mismatched version loading '%s', actual %d, expected %d\n", __FUNCTION__, filepath, map_version, MAP_VERSION);
          return false;
     }

     fread(player_start, sizeof(*player_start), 1, file);
     fread(&map_width, sizeof(map_width), 1, file);
     fread(&map_height, sizeof(map_height), 1, file);
     fread(&block_count, sizeof(block_count), 1, file);
     fread(&interactive_count, sizeof(interactive_count), 1, file);

     // alloc and convert map elements to map format
     S32 map_tile_count = (S32)(map_width) * (S32)(map_height);
     MapTileV1_t* map_tiles = (MapTileV1_t*)(calloc(map_tile_count, sizeof(*map_tiles)));
     if(!map_tiles){
          LOG("%s(): failed to allocate %d tiles\n", __FUNCTION__, map_tile_count);
          return false;
     }

     MapBlockV1_t* map_blocks = (MapBlockV1_t*)(calloc(block_count, sizeof(*map_blocks)));
     if(!map_blocks){
          LOG("%s(): failed to allocate %d blocks\n", __FUNCTION__, block_count);
          return false;
     }

     MapInteractiveV1_t* map_interactives = (MapInteractiveV1_t*)(calloc(interactive_count, sizeof(*map_interactives)));
     if(!map_interactives){
          LOG("%s(): failed to allocate %d interactives\n", __FUNCTION__, interactive_count);
          return false;
     }

     // read data from file
     fread(map_tiles, sizeof(*map_tiles), map_tile_count, file);
     fread(map_blocks, sizeof(*map_blocks), block_count, file);
     fread(map_interactives, sizeof(*map_interactives), interactive_count, file);

     destroy(tilemap);
     init(tilemap, map_width, map_height);

     destroy(block_array);
     init(block_array, block_count);

     destroy(interactive_array);
     init(interactive_array, interactive_count);

     // convert to map formats
     S32 index = 0;
     for(S32 y = 0; y < tilemap->height; y++){
          for(S32 x = 0; x < tilemap->width; x++){
               tilemap->tiles[y][x].id = map_tiles[index].id;
               tilemap->tiles[y][x].flags = map_tiles[index].flags;
               tilemap->tiles[y][x].light = BASE_LIGHT;
               index++;
          }
     }

     // TODO: a lot of maps have -16, -16 as the first block
     for(S16 i = 0; i < block_count; i++){
          Block_t* block = block_array->elements + i;
          block->pos.pixel = map_blocks[i].pixel;
          block->pos.z = map_blocks[i].z;
          block->face = map_blocks[i].face;
          block->element = map_blocks[i].element;
     }

     for(S16 i = 0; i < interactive_array->count; i++){
          Interactive_t* interactive = interactive_array->elements + i;
          interactive->coord = map_interactives[i].coord;
          interactive->type = map_interactives[i].type;

          switch(map_interactives[i].type){
          default:
          case INTERACTIVE_TYPE_LEVER:
          case INTERACTIVE_TYPE_BOW:
               break;
          case INTERACTIVE_TYPE_PRESSURE_PLATE:
               interactive->pressure_plate = map_interactives[i].pressure_plate;
               break;
          case INTERACTIVE_TYPE_LIGHT_DETECTOR:
          case INTERACTIVE_TYPE_ICE_DETECTOR:
               interactive->detector = map_interactives[i].detector;
               break;
          case INTERACTIVE_TYPE_POPUP:
               interactive->popup.lift.up = map_interactives[i].popup.up;
               interactive->popup.lift.timer = 0.0f;
               interactive->popup.iced = map_interactives[i].popup.iced;
               if(interactive->popup.lift.up){
                    interactive->popup.lift.ticks = HEIGHT_INTERVAL + 1;
               }else{
                    interactive->popup.lift.ticks = 1;
               }
               break;
          case INTERACTIVE_TYPE_DOOR:
               interactive->door.lift.up = map_interactives[i].door.up;
               interactive->door.lift.timer = 0.0f;
               interactive->door.face = map_interactives[i].door.face;
               break;
          case INTERACTIVE_TYPE_PORTAL:
               interactive->portal.face = map_interactives[i].portal.face;
               interactive->portal.on = map_interactives[i].portal.on;
               break;
          case INTERACTIVE_TYPE_STAIRS:
               interactive->stairs.up = map_interactives[i].stairs.up;
               interactive->stairs.face = map_interactives[i].stairs.face;
               break;
          case INTERACTIVE_TYPE_PROMPT:
               break;
          }
     }

     free(map_tiles);
     free(map_blocks);
     free(map_interactives);

     return true;
}

bool load_map(const char* filepath, Coord_t* player_start, TileMap_t* tilemap, ObjectArray_t<Block_t>* block_array,
              ObjectArray_t<Interactive_t>* interactive_array){
     FILE* f = fopen(filepath, "rb");
     if(!f){
          LOG("%s(): fopen() failed\n", __FUNCTION__);
          return false;
     }
     bool success = load_map_from_file(f, player_start, tilemap, block_array, interactive_array, filepath);
     fclose(f);
     return success;
}

bool load_map_number(S32 map_number, Coord_t* player_start, TileMap_t* tilemap, ObjectArray_t<Block_t>* block_array,
                     ObjectArray_t<Interactive_t>* interactive_array){
     // search through directory to find file starting with 3 digit map number
     DIR* d = opendir("content");
     if(!d) return false;
     struct dirent* dir;
     char filepath[64] = {};
     char match[4] = {};
     snprintf(match, 4, "%03d", map_number);
     while((dir = readdir(d)) != nullptr){
          if(strncmp(dir->d_name, match, 3) == 0 &&
             strstr(dir->d_name, ".bm")){ // TODO: create strendswith() func for this?
               snprintf(filepath, 64, "content/%s", dir->d_name);
               break;
          }
     }

     if(!filepath[0]) return false;

     LOG("load map %s\n", filepath);
     return load_map(filepath, player_start, tilemap, block_array, interactive_array);
}

enum StampType_t{
     STAMP_TYPE_NONE,
     STAMP_TYPE_TILE_ID,
     STAMP_TYPE_TILE_FLAGS,
     STAMP_TYPE_BLOCK,
     STAMP_TYPE_INTERACTIVE,
};

struct StampBlock_t{
     Element_t element;
     Direction_t face;
};

struct Stamp_t{
     StampType_t type;

     union{
          U8 tile_id;
          U16 tile_flags;
          StampBlock_t block;
          Interactive_t interactive;
     };

     Coord_t offset;
};

enum EditorMode_t : U8{
     EDITOR_MODE_OFF,
     EDITOR_MODE_CATEGORY_SELECT,
     EDITOR_MODE_STAMP_SELECT,
     EDITOR_MODE_STAMP_HIDE,
     EDITOR_MODE_CREATE_SELECTION,
     EDITOR_MODE_SELECTION_MANIPULATION,
};

Coord_t stamp_array_dimensions(ObjectArray_t<Stamp_t>* object_array){
     Coord_t min {0, 0};
     Coord_t max {0, 0};

     for(S32 i = 0; i < object_array->count; ++i){
          Stamp_t* stamp = object_array->elements + i;

          if(stamp->offset.x < min.x){min.x = stamp->offset.x;}
          if(stamp->offset.y < min.y){min.y = stamp->offset.y;}

          if(stamp->offset.x > max.x){max.x = stamp->offset.x;}
          if(stamp->offset.y > max.y){max.y = stamp->offset.y;}
     }

     return (max - min) + Coord_t{1, 1};
}

enum EditorCategory_t : U8{
     EDITOR_CATEGORY_TILE_ID,
     EDITOR_CATEGORY_TILE_FLAGS,
     EDITOR_CATEGORY_BLOCK,
     EDITOR_CATEGORY_INTERACTIVE_LEVER,
     EDITOR_CATEGORY_INTERACTIVE_PRESSURE_PLATE,
     EDITOR_CATEGORY_INTERACTIVE_POPUP,
     EDITOR_CATEGORY_INTERACTIVE_DOOR,
     EDITOR_CATEGORY_INTERACTIVE_LIGHT_DETECTOR,
     EDITOR_CATEGORY_INTERACTIVE_ICE_DETECTOR,
     EDITOR_CATEGORY_INTERACTIVE_BOW,
     EDITOR_CATEGORY_COUNT,
};

struct Editor_t{
     ObjectArray_t<ObjectArray_t<ObjectArray_t<Stamp_t>>> category_array;
     EditorMode_t mode = EDITOR_MODE_OFF;

     S32 category = 0;
     S32 stamp = 0;

     Coord_t selection_start;
     Coord_t selection_end;

     Coord_t clipboard_start_offset;
     Coord_t clipboard_end_offset;

     ObjectArray_t<Stamp_t> selection;
     ObjectArray_t<Stamp_t> clipboard;
};

bool init(Editor_t* editor){
     memset(editor, 0, sizeof(*editor));
     init(&editor->category_array, EDITOR_CATEGORY_COUNT);

     auto* tile_category = editor->category_array.elements + EDITOR_CATEGORY_TILE_ID;
     init(tile_category, 26);
     for(S16 i = 0; i < 14; i++){
          init(&tile_category->elements[i], 1);
          tile_category->elements[i].elements[0].type = STAMP_TYPE_TILE_ID;
          tile_category->elements[i].elements[0].tile_id = (U8)(i);
     }

     auto* tile_id_array = tile_category->elements + 13;

     tile_id_array++;
     init(tile_id_array, 2);
     tile_id_array->elements[0].type = STAMP_TYPE_TILE_ID;
     tile_id_array->elements[0].tile_id = 32;
     tile_id_array->elements[1].type = STAMP_TYPE_TILE_ID;
     tile_id_array->elements[1].tile_id = 16;
     tile_id_array->elements[1].offset = Coord_t{0, 1};

     tile_id_array++;
     init(tile_id_array, 2);
     tile_id_array->elements[0].type = STAMP_TYPE_TILE_ID;
     tile_id_array->elements[0].tile_id = 33;
     tile_id_array->elements[1].type = STAMP_TYPE_TILE_ID;
     tile_id_array->elements[1].tile_id = 17;
     tile_id_array->elements[1].offset = Coord_t{0, 1};

     tile_id_array++;
     init(tile_id_array, 2);
     tile_id_array->elements[0].type = STAMP_TYPE_TILE_ID;
     tile_id_array->elements[0].tile_id = 18;
     tile_id_array->elements[1].type = STAMP_TYPE_TILE_ID;
     tile_id_array->elements[1].tile_id = 19;
     tile_id_array->elements[1].offset = Coord_t{1, 0};

     tile_id_array++;
     init(tile_id_array, 2);
     tile_id_array->elements[0].type = STAMP_TYPE_TILE_ID;
     tile_id_array->elements[0].tile_id = 34;
     tile_id_array->elements[1].type = STAMP_TYPE_TILE_ID;
     tile_id_array->elements[1].tile_id = 35;
     tile_id_array->elements[1].offset = Coord_t{1, 0};

     for(S16 i = 0; i < 6; i++){
          tile_id_array++;
          init(tile_id_array, 4);
          tile_id_array->elements[0].type = STAMP_TYPE_TILE_ID;
          tile_id_array->elements[0].tile_id = 36 + (i * 2);
          tile_id_array->elements[1].type = STAMP_TYPE_TILE_ID;
          tile_id_array->elements[1].tile_id = 37 + (i * 2);
          tile_id_array->elements[1].offset = Coord_t{1, 0};
          tile_id_array->elements[2].type = STAMP_TYPE_TILE_ID;
          tile_id_array->elements[2].tile_id = 20 + (i * 2);
          tile_id_array->elements[2].offset = Coord_t{0, 1};
          tile_id_array->elements[3].type = STAMP_TYPE_TILE_ID;
          tile_id_array->elements[3].tile_id = 21 + (i * 2);
          tile_id_array->elements[3].offset = Coord_t{1, 1};
     }

     for(S16 i = 0; i < 2; i++){
          tile_id_array++;
          init(tile_id_array, 4);
          tile_id_array->elements[0].type = STAMP_TYPE_TILE_ID;
          tile_id_array->elements[0].tile_id = 50 + i * 4;
          tile_id_array->elements[1].type = STAMP_TYPE_TILE_ID;
          tile_id_array->elements[1].tile_id = 51 + i * 4;
          tile_id_array->elements[1].offset = Coord_t{1, 0};
          tile_id_array->elements[2].type = STAMP_TYPE_TILE_ID;
          tile_id_array->elements[2].tile_id = 48 + i * 4;
          tile_id_array->elements[2].offset = Coord_t{0, 1};
          tile_id_array->elements[3].type = STAMP_TYPE_TILE_ID;
          tile_id_array->elements[3].tile_id = 49 + i * 4;
          tile_id_array->elements[3].offset = Coord_t{1, 1};
     }

     auto* tile_flags_category = editor->category_array.elements + EDITOR_CATEGORY_TILE_FLAGS;
     init(tile_flags_category, 62);
     for(S8 i = 0; i < 2; i++){
          S8 index_offset = i * 15;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_LEFT;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_UP;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_RIGHT;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_DOWN;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_LEFT | TILE_FLAG_WIRE_DOWN;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_LEFT | TILE_FLAG_WIRE_RIGHT;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_LEFT | TILE_FLAG_WIRE_UP;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_LEFT | TILE_FLAG_WIRE_UP | TILE_FLAG_WIRE_RIGHT;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_LEFT | TILE_FLAG_WIRE_UP | TILE_FLAG_WIRE_DOWN;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_LEFT | TILE_FLAG_WIRE_DOWN | TILE_FLAG_WIRE_RIGHT;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_LEFT | TILE_FLAG_WIRE_UP | TILE_FLAG_WIRE_DOWN | TILE_FLAG_WIRE_RIGHT;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_UP | TILE_FLAG_WIRE_RIGHT;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_UP | TILE_FLAG_WIRE_DOWN;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_UP | TILE_FLAG_WIRE_DOWN | TILE_FLAG_WIRE_RIGHT;
          index_offset++;
          init(&tile_flags_category->elements[index_offset], 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_RIGHT | TILE_FLAG_WIRE_DOWN;
     }

     for(S8 i = 0; i < 15; i++){
          tile_flags_category->elements[15 + i].elements[0].tile_flags |= TILE_FLAG_WIRE_STATE;
     }

     for(S8 i = 0; i < DIRECTION_COUNT; i++){
          S8 index_offset = 30 + (i * 7);
          init(tile_flags_category->elements + index_offset, 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_CLUSTER_LEFT;
          tile_flags_set_cluster_direction(&tile_flags_category->elements[index_offset].elements[0].tile_flags, (Direction_t)(i));
          index_offset++;
          init(tile_flags_category->elements + index_offset, 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_CLUSTER_MID;
          tile_flags_set_cluster_direction(&tile_flags_category->elements[index_offset].elements[0].tile_flags, (Direction_t)(i));
          index_offset++;
          init(tile_flags_category->elements + index_offset, 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_CLUSTER_RIGHT;
          tile_flags_set_cluster_direction(&tile_flags_category->elements[index_offset].elements[0].tile_flags, (Direction_t)(i));
          index_offset++;
          init(tile_flags_category->elements + index_offset, 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_CLUSTER_LEFT | TILE_FLAG_WIRE_CLUSTER_MID;
          tile_flags_set_cluster_direction(&tile_flags_category->elements[index_offset].elements[0].tile_flags, (Direction_t)(i));
          index_offset++;
          init(tile_flags_category->elements + index_offset, 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_CLUSTER_LEFT | TILE_FLAG_WIRE_CLUSTER_RIGHT;
          tile_flags_set_cluster_direction(&tile_flags_category->elements[index_offset].elements[0].tile_flags, (Direction_t)(i));
          index_offset++;
          init(tile_flags_category->elements + index_offset, 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_CLUSTER_MID | TILE_FLAG_WIRE_CLUSTER_RIGHT;
          tile_flags_set_cluster_direction(&tile_flags_category->elements[index_offset].elements[0].tile_flags, (Direction_t)(i));
          index_offset++;
          init(tile_flags_category->elements + index_offset, 1);
          tile_flags_category->elements[index_offset].elements[0].type = STAMP_TYPE_TILE_FLAGS;
          tile_flags_category->elements[index_offset].elements[0].tile_flags = TILE_FLAG_WIRE_CLUSTER_LEFT | TILE_FLAG_WIRE_CLUSTER_MID | TILE_FLAG_WIRE_CLUSTER_RIGHT;
          tile_flags_set_cluster_direction(&tile_flags_category->elements[index_offset].elements[0].tile_flags, (Direction_t)(i));
     }

     init(tile_flags_category->elements + 59, 1);
     tile_flags_category->elements[59].elements[0].type = STAMP_TYPE_TILE_FLAGS;
     tile_flags_category->elements[59].elements[0].tile_flags = TILE_FLAG_ICED;
     init(tile_flags_category->elements + 60, 1);
     tile_flags_category->elements[60].elements[0].type = STAMP_TYPE_TILE_FLAGS;
     tile_flags_category->elements[60].elements[0].tile_flags = TILE_FLAG_CHECKPOINT;
     init(tile_flags_category->elements + 61, 1);
     tile_flags_category->elements[61].elements[0].type = STAMP_TYPE_TILE_FLAGS;
     tile_flags_category->elements[61].elements[0].tile_flags = TILE_FLAG_RESET_IMMUNE;

     auto* block_category = editor->category_array.elements + EDITOR_CATEGORY_BLOCK;
     init(block_category, 4);
     for(S16 i = 0; i < block_category->count; i++){
          init(block_category->elements + i, 1);
          block_category->elements[i].elements[0].type = STAMP_TYPE_BLOCK;
          block_category->elements[i].elements[0].block.face = DIRECTION_LEFT;
          block_category->elements[i].elements[0].block.element = (Element_t)(i);
     }

     auto* interactive_lever_category = editor->category_array.elements + EDITOR_CATEGORY_INTERACTIVE_LEVER;
     init(interactive_lever_category, 1);
     init(interactive_lever_category->elements, 1);
     interactive_lever_category->elements[0].elements[0].type = STAMP_TYPE_INTERACTIVE;
     interactive_lever_category->elements[0].elements[0].interactive.type = INTERACTIVE_TYPE_LEVER;

     auto* interactive_pressure_plate_category = editor->category_array.elements + EDITOR_CATEGORY_INTERACTIVE_PRESSURE_PLATE;
     init(interactive_pressure_plate_category, 2);
     init(interactive_pressure_plate_category->elements, 1);
     interactive_pressure_plate_category->elements[0].elements[0].type = STAMP_TYPE_INTERACTIVE;
     interactive_pressure_plate_category->elements[0].elements[0].interactive.type = INTERACTIVE_TYPE_PRESSURE_PLATE;
     init(interactive_pressure_plate_category->elements + 1, 1);
     interactive_pressure_plate_category->elements[1].elements[0].type = STAMP_TYPE_INTERACTIVE;
     interactive_pressure_plate_category->elements[1].elements[0].interactive.type = INTERACTIVE_TYPE_PRESSURE_PLATE;
     interactive_pressure_plate_category->elements[1].elements[0].interactive.pressure_plate.iced_under = true;

     auto* interactive_popup_category = editor->category_array.elements + EDITOR_CATEGORY_INTERACTIVE_POPUP;
     init(interactive_popup_category, 2);
     init(interactive_popup_category->elements, 1);
     interactive_popup_category->elements[0].elements[0].type = STAMP_TYPE_INTERACTIVE;
     interactive_popup_category->elements[0].elements[0].interactive.type = INTERACTIVE_TYPE_POPUP;
     interactive_popup_category->elements[0].elements[0].interactive.popup.lift.ticks = HEIGHT_INTERVAL + 1;
     interactive_popup_category->elements[0].elements[0].interactive.popup.lift.up = true;
     init(interactive_popup_category->elements + 1, 1);
     interactive_popup_category->elements[1].elements[0].type = STAMP_TYPE_INTERACTIVE;
     interactive_popup_category->elements[1].elements[0].interactive.type = INTERACTIVE_TYPE_POPUP;
     interactive_popup_category->elements[1].elements[0].interactive.popup.lift.ticks = 1;
     interactive_popup_category->elements[1].elements[0].interactive.popup.lift.up = false;

     auto* interactive_door_category = editor->category_array.elements + EDITOR_CATEGORY_INTERACTIVE_DOOR;
     init(interactive_door_category, 8);
     for(S8 j = 0; j < DIRECTION_COUNT; j++){
          init(interactive_door_category->elements + j, 1);
          interactive_door_category->elements[j].elements[0].type = STAMP_TYPE_INTERACTIVE;
          interactive_door_category->elements[j].elements[0].interactive.type = INTERACTIVE_TYPE_DOOR;
          interactive_door_category->elements[j].elements[0].interactive.door.lift.ticks = DOOR_MAX_HEIGHT;
          interactive_door_category->elements[j].elements[0].interactive.door.lift.up = true;
          interactive_door_category->elements[j].elements[0].interactive.door.face = (Direction_t)(j);
     }

     for(S8 j = 0; j < DIRECTION_COUNT; j++){
          init(interactive_door_category->elements + j + 4, 1);
          interactive_door_category->elements[j + 4].elements[0].type = STAMP_TYPE_INTERACTIVE;
          interactive_door_category->elements[j + 4].elements[0].interactive.type = INTERACTIVE_TYPE_DOOR;
          interactive_door_category->elements[j + 4].elements[0].interactive.door.lift.ticks = 0;
          interactive_door_category->elements[j + 4].elements[0].interactive.door.lift.up = false;
          interactive_door_category->elements[j + 4].elements[0].interactive.door.face = (Direction_t)(j);

     }

     auto* interactive_light_detector_category = editor->category_array.elements + EDITOR_CATEGORY_INTERACTIVE_LIGHT_DETECTOR;
     init(interactive_light_detector_category, 1);
     init(interactive_light_detector_category->elements, 1);
     interactive_light_detector_category->elements[0].elements[0].type = STAMP_TYPE_INTERACTIVE;
     interactive_light_detector_category->elements[0].elements[0].interactive.type = INTERACTIVE_TYPE_LIGHT_DETECTOR;

     auto* interactive_ice_detector_category = editor->category_array.elements + EDITOR_CATEGORY_INTERACTIVE_ICE_DETECTOR;
     init(interactive_ice_detector_category, 1);
     init(interactive_ice_detector_category->elements, 1);
     interactive_ice_detector_category->elements[0].elements[0].type = STAMP_TYPE_INTERACTIVE;
     interactive_ice_detector_category->elements[0].elements[0].interactive.type = INTERACTIVE_TYPE_ICE_DETECTOR;

     auto* interactive_bow_category = editor->category_array.elements + EDITOR_CATEGORY_INTERACTIVE_BOW;
     init(interactive_bow_category, 1);
     init(interactive_bow_category->elements, 1);
     interactive_bow_category->elements[0].elements[0].type = STAMP_TYPE_INTERACTIVE;
     interactive_bow_category->elements[0].elements[0].interactive.type = INTERACTIVE_TYPE_BOW;

     return true;
}

void destroy(Editor_t* editor){
     for(S16 i = 0; i < editor->category_array.count; i++){
          destroy(editor->category_array.elements + i);
     }

     destroy(&editor->category_array);
     destroy(&editor->selection);
     destroy(&editor->clipboard);
}

struct UndoBlock_t{
     Pixel_t pixel;
     S8 z;
     Element_t element;
     Vec_t accel;
     Vec_t vel;
};

struct UndoPlayer_t{
     Pixel_t pixel;
     S8 z;
     Direction_t face;
};

enum UndoDiffType_t : U8{
     UNDO_DIFF_TYPE_PLAYER,
     UNDO_DIFF_TYPE_TILE_FLAGS,
     UNDO_DIFF_TYPE_BLOCK,
     UNDO_DIFF_TYPE_INTERACTIVE,
};

struct UndoDiffHeader_t{
     S32 index;
     UndoDiffType_t type;
};

struct UndoHistory_t{
     U32 size;
     void* start;
     void* current;
};

struct Undo_t{
     S16 width;
     S16 height;
     U16** tile_flags;
     ObjectArray_t<UndoBlock_t> block_array;
     ObjectArray_t<Interactive_t> interactive_array;
     UndoPlayer_t player;

     UndoHistory_t history;
};

bool init(UndoHistory_t* undo_history, U32 history_size){
     undo_history->start = malloc(history_size);
     if(!undo_history->start) return false;
     undo_history->current = undo_history->start;
     undo_history->size = history_size;
     return true;
}

#define ASSERT_BELOW_HISTORY_SIZE(history) assert((char*)(history->current) - (char*)(history->start) < history->size)

void undo_history_add(UndoHistory_t* undo_history, UndoDiffType_t type, S32 index){
     switch(type){
     default:
          assert(!"unsupported diff type");
          return;
     case UNDO_DIFF_TYPE_PLAYER:
          undo_history->current = (char*)(undo_history->current) + sizeof(UndoPlayer_t);
          break;
     case UNDO_DIFF_TYPE_TILE_FLAGS:
          undo_history->current = (char*)(undo_history->current) + sizeof(U16);
          break;
     case UNDO_DIFF_TYPE_BLOCK:
          undo_history->current = (char*)(undo_history->current) + sizeof(UndoBlock_t);
          break;
     case UNDO_DIFF_TYPE_INTERACTIVE:
          undo_history->current = (char*)(undo_history->current) + sizeof(Interactive_t);
          break;
     }

     ASSERT_BELOW_HISTORY_SIZE(undo_history);

     auto* undo_header = (UndoDiffHeader_t*)(undo_history->current);
     undo_header->type = type;
     undo_header->index = index;
     undo_history->current = (char*)(undo_history->current) + sizeof(*undo_header);

     ASSERT_BELOW_HISTORY_SIZE(undo_history);
}

bool init(Undo_t* undo, U32 history_size, S16 map_width, S16 map_height, S16 block_count, S16 interactive_count){
     undo->tile_flags = (U16**)calloc(map_height, sizeof(*undo->tile_flags));
     if(!undo->tile_flags) return false;
     for(S16 i = 0; i < map_height; i++){
          undo->tile_flags[i] = (U16*)calloc(map_width, sizeof(*undo->tile_flags[i]));
          if(!undo->tile_flags[i]) return false;
     }
     undo->width = map_width;
     undo->height = map_height;

     if(!init(&undo->block_array, block_count)) return false;
     if(!init(&undo->interactive_array, interactive_count)) return false;
     if(!init(&undo->history, history_size)) return false;

     return true;
}

void destroy(Undo_t* undo){
     for(int i = 0; i < undo->height; i++){
          free(undo->tile_flags[i]);
     }
     free(undo->tile_flags);
     undo->tile_flags = nullptr;
     undo->width = 0;
     undo->height = 0;
     destroy(&undo->block_array);
     destroy(&undo->interactive_array);
}

void undo_snapshot(Undo_t* undo, Player_t* player, TileMap_t* tilemap, ObjectArray_t<Block_t>* block_array,
                   ObjectArray_t<Interactive_t>* interactive_array){
     undo->player.pixel = player->pos.pixel;
     undo->player.z = player->pos.z;
     undo->player.face = player->face;

     for(S16 y = 0; y < tilemap->height; y++){
          for(S16 x = 0; x < tilemap->width; x++){
               undo->tile_flags[y][x] = tilemap->tiles[y][x].flags;
          }
     }

     for(S16 i = 0; i < block_array->count; i++){
          UndoBlock_t* undo_block = undo->block_array.elements + i;
          Block_t* block = block_array->elements + i;
          undo_block->pixel = block->pos.pixel;
          undo_block->z = block->pos.z;
          undo_block->element = block->element;
          undo_block->accel = block->accel;
          undo_block->vel = block->vel;
     }

     for(S16 i = 0; i < interactive_array->count; i++){
          undo->interactive_array.elements[i] = interactive_array->elements[i];
     }
}

void undo_commit(Undo_t* undo, Player_t* player, TileMap_t* tilemap, ObjectArray_t<Block_t>* block_array,
                 ObjectArray_t<Interactive_t>* interactive_array){
     U32 diff_count = 0;
     if(player->pos.pixel != undo->player.pixel ||
        player->pos.z != undo->player.z ||
        player->face != undo->player.face){
          auto* undo_player = (UndoPlayer_t*)(undo->history.current);
          *undo_player = undo->player;
          undo_history_add(&undo->history, UNDO_DIFF_TYPE_PLAYER, 0);
          diff_count++;
     }

     for(S16 y = 0; y < tilemap->height; y++){
          for(S16 x = 0; x < tilemap->width; x++){
               if(undo->tile_flags[y][x] != tilemap->tiles[y][x].flags){
                    auto* undo_tile_flags = (U16*)(undo->history.current);
                    *undo_tile_flags = undo->tile_flags[y][x];
                    undo_history_add(&undo->history, UNDO_DIFF_TYPE_TILE_FLAGS, y * tilemap->width + x);
                    diff_count++;

               }
          }
     }

     for(S16 i = 0; i < block_array->count; i++){
          UndoBlock_t* undo_block = undo->block_array.elements + i;
          Block_t* block = block_array->elements + i;

          if(undo_block->pixel != block->pos.pixel ||
             undo_block->z != block->pos.z ||
             undo_block->element != block->element){
               auto* undo_block_entry = (UndoBlock_t*)(undo->history.current);
               *undo_block_entry = *undo_block;
               undo_history_add(&undo->history, UNDO_DIFF_TYPE_BLOCK, i);
               diff_count++;
          }
     }

     for(S16 i = 0; i < interactive_array->count; i++){
          Interactive_t* undo_interactive = undo->interactive_array.elements + i;
          Interactive_t* interactive = interactive_array->elements + i;
          assert(undo_interactive->type == interactive->type);

          bool diff = false;

          switch(interactive->type){
          default:
               break;
          case INTERACTIVE_TYPE_PRESSURE_PLATE:
               if(undo_interactive->pressure_plate.down != interactive->pressure_plate.down ||
                  undo_interactive->pressure_plate.iced_under != interactive->pressure_plate.iced_under){
                    diff = true;
               }
               break;
          case INTERACTIVE_TYPE_ICE_DETECTOR:
          case INTERACTIVE_TYPE_LIGHT_DETECTOR:
               if(undo_interactive->detector.on != interactive->detector.on){
                    diff = true;
               }
               break;
          case INTERACTIVE_TYPE_POPUP:
               if(undo_interactive->popup.iced != interactive->popup.iced ||
                  undo_interactive->popup.lift.up != interactive->popup.lift.up ||
                  undo_interactive->popup.lift.ticks != interactive->popup.lift.ticks){
                    diff = true;
               }
               break;
          case INTERACTIVE_TYPE_LEVER:
               // TODO
               break;
          case INTERACTIVE_TYPE_DOOR:
               if(undo_interactive->door.lift.up != interactive->door.lift.up ||
                  undo_interactive->door.lift.ticks != interactive->door.lift.ticks){
                    diff = true;
               }
               break;
          case INTERACTIVE_TYPE_PORTAL:
               if(undo_interactive->portal.on != interactive->portal.on){
                    diff = true;
               }
               break;
          case INTERACTIVE_TYPE_BOW:
               break;
          }

          if(diff){
               auto* undo_interactive_entry = (Interactive_t*)(undo->history.current);
               *undo_interactive_entry = *undo_interactive;
               undo_history_add(&undo->history, UNDO_DIFF_TYPE_INTERACTIVE, i);
               diff_count++;
          }
     }

     auto* count_entry = (S32*)(undo->history.current);
     *count_entry = diff_count;
     undo->history.current = (char*)(undo->history.current) + sizeof(S32);
     ASSERT_BELOW_HISTORY_SIZE((&undo->history));

     undo_snapshot(undo, player, tilemap, block_array, interactive_array);
}

void undo_revert(Undo_t* undo, Player_t* player, TileMap_t* tilemap, ObjectArray_t<Block_t>* block_array,
                 ObjectArray_t<Interactive_t>* interactive_array){
     if(undo->history.current <= undo->history.start) return;

     auto* ptr = (char*)(undo->history.current);
     S32 diff_count = 0;
     ptr -= sizeof(diff_count);
     diff_count = *(S32*)(ptr);

     for(S32 i = 0; i < diff_count; i++){
          ptr -= sizeof(UndoDiffHeader_t);
          auto* diff_header = (UndoDiffHeader_t*)(ptr);

          switch(diff_header->type){
          default:
               assert(!"memory probably corrupted, or new unsupported diff type");
               return;
          case UNDO_DIFF_TYPE_PLAYER:
          {
               ptr -= sizeof(UndoPlayer_t);
               auto* player_entry = (UndoPlayer_t*)(ptr);
               *player = {};
               // TODO fix these numbers as they are important
               player->walk_frame_delta = 1;
               player->radius = 3.5f / 272.0f;
               player->pos.pixel = player_entry->pixel;
               player->pos.z = player_entry->z;
               player->face = player_entry->face;
          } break;
          case UNDO_DIFF_TYPE_TILE_FLAGS:
          {
               int x = diff_header->index % tilemap->width;
               int y = diff_header->index / tilemap->width;

               ptr -= sizeof(U16);
               auto* tile_flags_entry = (U16*)(ptr);
               tilemap->tiles[y][x].flags = *tile_flags_entry;
          } break;
          case UNDO_DIFF_TYPE_BLOCK:
          {
               ptr -= sizeof(UndoBlock_t);
               auto* block_entry = (UndoBlock_t*)(ptr);
               Block_t* block = block_array->elements + diff_header->index;
               block->pos.pixel = block_entry->pixel;
               block->pos.z = block_entry->z;
               block->element = block_entry->element;
               block->accel = block_entry->accel;
               block->vel = block_entry->vel;
          } break;
          case UNDO_DIFF_TYPE_INTERACTIVE:
          {
               ptr -= sizeof(Interactive_t);
               auto* interactive_entry = (Interactive_t*)(ptr);
               interactive_array->elements[diff_header->index] = *interactive_entry;
          } break;
          }
     }

     undo->history.current = ptr;
}

void draw_theme_frame(Vec_t tex_vec, Vec_t pos_vec){
     glTexCoord2f(tex_vec.x, tex_vec.y);
     glVertex2f(pos_vec.x, pos_vec.y);
     glTexCoord2f(tex_vec.x, tex_vec.y + THEME_FRAME_HEIGHT);
     glVertex2f(pos_vec.x, pos_vec.y + TILE_SIZE);
     glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y + THEME_FRAME_HEIGHT);
     glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y + TILE_SIZE);
     glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y);
     glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y);
}

void draw_double_theme_frame(Vec_t tex_vec, Vec_t pos_vec){
     glTexCoord2f(tex_vec.x, tex_vec.y);
     glVertex2f(pos_vec.x, pos_vec.y);
     glTexCoord2f(tex_vec.x, tex_vec.y + 2.0f * THEME_FRAME_HEIGHT);
     glVertex2f(pos_vec.x, pos_vec.y + 2.0f * TILE_SIZE);
     glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y + 2.0f * THEME_FRAME_HEIGHT);
     glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y + 2.0f * TILE_SIZE);
     glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y);
     glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y);
}

void tile_id_draw(U8 id, Vec_t pos){
     U8 id_x = id % 16;
     U8 id_y = id / 16;

     draw_theme_frame(theme_frame(id_x, id_y), pos);
}

void tile_flags_draw(U16 flags, Vec_t tile_pos){
     if(flags == 0) return;

     if(flags & TILE_FLAG_CHECKPOINT){
          draw_theme_frame(theme_frame(0, 21), tile_pos);
     }

     if(flags & TILE_FLAG_RESET_IMMUNE){
          draw_theme_frame(theme_frame(1, 21), tile_pos);
     }

     if(flags & (TILE_FLAG_WIRE_LEFT | TILE_FLAG_WIRE_RIGHT | TILE_FLAG_WIRE_UP | TILE_FLAG_WIRE_DOWN)){
          S16 frame_y = 9;
          S16 frame_x = flags >> 4;

          if(flags & TILE_FLAG_WIRE_STATE) frame_y++;

          draw_theme_frame(theme_frame(frame_x, frame_y), tile_pos);
     }

     if(flags & (TILE_FLAG_WIRE_CLUSTER_LEFT | TILE_FLAG_WIRE_CLUSTER_MID | TILE_FLAG_WIRE_CLUSTER_RIGHT)){
          S16 frame_y = 17 + tile_flags_cluster_direction(flags);
          S16 frame_x = 0;

          if(flags & TILE_FLAG_WIRE_CLUSTER_LEFT){
               if(flags & TILE_FLAG_WIRE_CLUSTER_LEFT_ON){
                    frame_x = 1;
               }else{
                    frame_x = 0;
               }

               draw_theme_frame(theme_frame(frame_x, frame_y), tile_pos);
          }

          if(flags & TILE_FLAG_WIRE_CLUSTER_MID){
               if(flags & TILE_FLAG_WIRE_CLUSTER_MID_ON){
                    frame_x = 3;
               }else{
                    frame_x = 2;
               }

               draw_theme_frame(theme_frame(frame_x, frame_y), tile_pos);
          }

          if(flags & TILE_FLAG_WIRE_CLUSTER_RIGHT){
               if(flags & TILE_FLAG_WIRE_CLUSTER_RIGHT_ON){
                    frame_x = 5;
               }else{
                    frame_x = 4;
               }

               draw_theme_frame(theme_frame(frame_x, frame_y), tile_pos);
          }
     }
}

void block_draw(Block_t* block, Vec_t pos_vec){
     Vec_t tex_vec = theme_frame(0, 6);
     glTexCoord2f(tex_vec.x, tex_vec.y);
     glVertex2f(pos_vec.x, pos_vec.y);
     glTexCoord2f(tex_vec.x, tex_vec.y + 2.0f * THEME_FRAME_HEIGHT);
     glVertex2f(pos_vec.x, pos_vec.y + 2.0f * TILE_SIZE);
     glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y + 2.0f * THEME_FRAME_HEIGHT);
     glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y + 2.0f * TILE_SIZE);
     glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y);
     glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y);

     if(block->element == ELEMENT_ONLY_ICED || block->element == ELEMENT_ICE ){
          tex_vec = theme_frame(4, 12);
          glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
          glTexCoord2f(tex_vec.x, tex_vec.y);
          glVertex2f(pos_vec.x, pos_vec.y);
          glTexCoord2f(tex_vec.x, tex_vec.y + 2.0f * THEME_FRAME_HEIGHT);
          glVertex2f(pos_vec.x, pos_vec.y + 2.0f * TILE_SIZE);
          glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y + 2.0f * THEME_FRAME_HEIGHT);
          glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y + 2.0f * TILE_SIZE);
          glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y);
          glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y);
          glColor3f(1.0f, 1.0f, 1.0f);
     }

     if(block->element == ELEMENT_FIRE){
          tex_vec = theme_frame(1, 6);
          // TODO: compress
          glTexCoord2f(tex_vec.x, tex_vec.y);
          glVertex2f(pos_vec.x, pos_vec.y);
          glTexCoord2f(tex_vec.x, tex_vec.y + 2.0f * THEME_FRAME_HEIGHT);
          glVertex2f(pos_vec.x, pos_vec.y + 2.0f * TILE_SIZE);
          glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y + 2.0f * THEME_FRAME_HEIGHT);
          glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y + 2.0f * TILE_SIZE);
          glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y);
          glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y);
     }else if(block->element == ELEMENT_ICE){
          tex_vec = theme_frame(5, 6);
          // TODO: compress
          glTexCoord2f(tex_vec.x, tex_vec.y);
          glVertex2f(pos_vec.x, pos_vec.y);
          glTexCoord2f(tex_vec.x, tex_vec.y + 2.0f * THEME_FRAME_HEIGHT);
          glVertex2f(pos_vec.x, pos_vec.y + 2.0f * TILE_SIZE);
          glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y + 2.0f * THEME_FRAME_HEIGHT);
          glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y + 2.0f * TILE_SIZE);
          glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y);
          glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y);
     }
}

void interactive_draw(Interactive_t* interactive, Vec_t pos_vec){
     Vec_t tex_vec = {};
     switch(interactive->type){
     default:
          break;
     case INTERACTIVE_TYPE_PRESSURE_PLATE:
     {
          int frame_x = 7;
          if(interactive->pressure_plate.down) frame_x++;
          tex_vec = theme_frame(frame_x, 8);
          draw_theme_frame(tex_vec, pos_vec);
     } break;
     case INTERACTIVE_TYPE_LEVER:
          tex_vec = theme_frame(0, 12);
          draw_double_theme_frame(tex_vec, pos_vec);
          break;
     case INTERACTIVE_TYPE_BOW:
          draw_theme_frame(theme_frame(0, 9), pos_vec);
          break;
     case INTERACTIVE_TYPE_POPUP:
          tex_vec = theme_frame(interactive->popup.lift.ticks - 1, 8);
          draw_double_theme_frame(tex_vec, pos_vec);
          break;
     case INTERACTIVE_TYPE_DOOR:
          tex_vec = theme_frame(interactive->door.lift.ticks + 8, 11 + interactive->door.face);
          draw_theme_frame(tex_vec, pos_vec);
          break;
     case INTERACTIVE_TYPE_LIGHT_DETECTOR:
          draw_theme_frame(theme_frame(1, 11), pos_vec);
          if(interactive->detector.on){
               draw_theme_frame(theme_frame(2, 11), pos_vec);
          }
          break;
     case INTERACTIVE_TYPE_ICE_DETECTOR:
          draw_theme_frame(theme_frame(1, 12), pos_vec);
          if(interactive->detector.on){
               draw_theme_frame(theme_frame(2, 12), pos_vec);
          }
          break;
     }

}

struct Quad_t{
     F32 left;
     F32 bottom;
     F32 right;
     F32 top;
};

void draw_quad_wireframe(const Quad_t* quad, F32 red, F32 green, F32 blue)
{
     glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

     glBegin(GL_QUADS);
     glColor3f(red, green, blue);
     glVertex2f(quad->left,  quad->top);
     glVertex2f(quad->left,  quad->bottom);
     glVertex2f(quad->right, quad->bottom);
     glVertex2f(quad->right, quad->top);
     glEnd();

     glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void selection_draw(Coord_t selection_start, Coord_t selection_end, Position_t camera, F32 red, F32 green, F32 blue)
{
     if(selection_start.x > selection_end.x) SWAP(selection_start.x, selection_end.x);
     if(selection_start.y > selection_end.y) SWAP(selection_start.y, selection_end.y);

     Position_t start_location = coord_to_pos(selection_start) - camera;
     Position_t end_location = coord_to_pos(selection_end) - camera;
     Vec_t start_vec = pos_to_vec(start_location);
     Vec_t end_vec = pos_to_vec(end_location);

     Quad_t selection_quad {start_vec.x, start_vec.y, end_vec.x + TILE_SIZE, end_vec.y + TILE_SIZE};
     glBindTexture(GL_TEXTURE_2D, 0);
     draw_quad_wireframe(&selection_quad, red, green, blue);
}

Coord_t mouse_select_coord(Vec_t mouse_screen)
{
     return {(S16)(mouse_screen.x * (F32)(ROOM_TILE_SIZE)), (S16)(mouse_screen.y * (F32)(ROOM_TILE_SIZE))};
}

Coord_t mouse_select_world(Vec_t mouse_screen, Position_t camera){
     return mouse_select_coord(mouse_screen) + (pos_to_coord(camera) - Coord_t{ROOM_TILE_SIZE / 2, ROOM_TILE_SIZE / 2});
}
S32 mouse_select_index(Vec_t mouse_screen)
{
     Coord_t coord = mouse_select_coord(mouse_screen);
     return coord.y * ROOM_TILE_SIZE + coord.x;
}

Vec_t coord_to_screen_position(Coord_t coord)
{
     Pixel_t pixel = coord_to_pixel(coord);
     Position_t relative_loc {pixel, 0, {0.0f, 0.0f}};
     return pos_to_vec(relative_loc);
}

void apply_stamp(Stamp_t* stamp, Coord_t coord, TileMap_t* tilemap, ObjectArray_t<Block_t>* block_array, ObjectArray_t<Interactive_t>* interactive_array,
                 QuadTreeNode_t<Interactive_t>** interactive_quad_tree, bool combine){
     switch(stamp->type){
     default:
          break;
     case STAMP_TYPE_TILE_ID:
     {
          Tile_t* tile = tilemap_get_tile(tilemap, coord);
          if(tile) tile->id = stamp->tile_id;
     } break;
     case STAMP_TYPE_TILE_FLAGS:
     {
          Tile_t* tile = tilemap_get_tile(tilemap, coord);
          if(tile){
               if(combine){
                    tile->flags |= stamp->tile_flags;
               }else{
                    tile->flags = stamp->tile_flags;
               }
          }
     } break;
     case STAMP_TYPE_BLOCK:
     {
          int index = block_array->count;
          resize(block_array, block_array->count + 1);
          // TODO: Check if block is in the way with the quad tree

          Block_t* block = block_array->elements + index;
          block->pos = coord_to_pos(coord);
          block->vel = vec_zero();
          block->accel = vec_zero();
          block->element = stamp->block.element;
          block->face = stamp->block.face;
     } break;
     case STAMP_TYPE_INTERACTIVE:
     {
          Interactive_t* interactive = quad_tree_interactive_find_at(*interactive_quad_tree, coord);
          if(interactive) return;

          int index = interactive_array->count;
          resize(interactive_array, interactive_array->count + 1);
          interactive_array->elements[index] = stamp->interactive;
          interactive_array->elements[index].coord = coord;
          quad_tree_free(*interactive_quad_tree);
          *interactive_quad_tree = quad_tree_build(interactive_array);
     } break;
     }
}

void coord_clear(Coord_t coord, TileMap_t* tilemap, ObjectArray_t<Interactive_t>* interactive_array,
                 QuadTreeNode_t<Interactive_t>* interactive_quad_tree, ObjectArray_t<Block_t>* block_array){
     Tile_t* tile = tilemap_get_tile(tilemap, coord);
     if(tile){
          tile->id = 0;
          tile->flags = 0;
     }

     auto* interactive = quad_tree_interactive_find_at(interactive_quad_tree, coord);
     if(interactive){
          S16 index = interactive - interactive_array->elements;
          if(index >= 0){
               remove(interactive_array, index);
               quad_tree_free(interactive_quad_tree);
               interactive_quad_tree = quad_tree_build(interactive_array);
          }
     }

     S16 block_index = -1;
     for(S16 i = 0; i < block_array->count; i++){
          if(pos_to_coord(block_array->elements[i].pos) == coord){
               block_index = i;
               break;
          }
     }

     if(block_index >= 0){
          remove(block_array, block_index);
     }
}

Rect_t editor_selection_bounds(Editor_t* editor){
     Rect_t rect {};
     for(S16 i = 0; i < editor->selection.count; i++){
          auto* stamp = editor->selection.elements + i;
          if(rect.left > stamp->offset.x) rect.left = stamp->offset.x;
          if(rect.bottom > stamp->offset.y) rect.bottom = stamp->offset.y;
          if(rect.right < stamp->offset.x) rect.right = stamp->offset.x;
          if(rect.top < stamp->offset.y) rect.top = stamp->offset.y;
     }

     rect.left += editor->selection_start.x;
     rect.right += editor->selection_start.x;
     rect.top += editor->selection_start.y;
     rect.bottom += editor->selection_start.y;

     return rect;
}

S32 mouse_select_stamp_index(Coord_t screen_coord, ObjectArray_t<ObjectArray_t<Stamp_t>>* stamp_array){
     S32 index = -1;
     Rect_t current_rect = {};
     S16 row_height = 0;
     for(S16 i = 0; i < stamp_array->count; i++){
          Coord_t dimensions = stamp_array_dimensions(stamp_array->elements + i);

          if(row_height < dimensions.y) row_height = dimensions.y; // track max

          current_rect.right = current_rect.left + dimensions.x;
          current_rect.top = current_rect.bottom + dimensions.y;

          if(screen_coord.x >= current_rect.left && screen_coord.x < current_rect.right &&
             screen_coord.y >= current_rect.bottom && screen_coord.y < current_rect.top){
               index = i;
               break;
          }

          current_rect.left += dimensions.x;

          // wrap around to next row if necessary
          if(current_rect.left >= ROOM_TILE_SIZE){
               current_rect.left = 0;
               current_rect.bottom += row_height;
          }
     }
     return index;
}

FILE* load_demo_number(S32 map_number, const char** demo_filepath){
     char filepath[64] = {};
     snprintf(filepath, 64, "content/%03d.bd", map_number);
     *demo_filepath = strdup(filepath);
     return fopen(*demo_filepath, "rb");
}

void reset_map(Player_t* player, Coord_t player_start, ObjectArray_t<Interactive_t>* interactive_array,
               QuadTreeNode_t<Interactive_t>** interactive_quad_tree){
     player_spawn(player, player_start);

     // update interactive quad tree
     quad_tree_free(*interactive_quad_tree);
     *interactive_quad_tree = quad_tree_build(interactive_array);
}

using namespace std::chrono;

int main(int argc, char** argv){
     DemoMode_t demo_mode = DEMO_MODE_NONE;
     const char* demo_filepath = nullptr;
     const char* load_map_filepath = nullptr;
     bool test = false;
     bool suite = false;
     bool show_suite = false;
     S16 map_number = 0;
     S16 first_map_number = 0;
     S16 map_count = 0;

     for(int i = 1; i < argc; i++){
          if(strcmp(argv[i], "-play") == 0){
               int next = i + 1;
               if(next >= argc) continue;
               demo_filepath = argv[next];
               demo_mode = DEMO_MODE_PLAY;
          }else if(strcmp(argv[i], "-record") == 0){
               int next = i + 1;
               if(next >= argc) continue;
               demo_filepath = argv[next];
               demo_mode = DEMO_MODE_RECORD;
          }else if(strcmp(argv[i], "-load") == 0){
               int next = i + 1;
               if(next >= argc) continue;
               load_map_filepath = argv[next];
          }else if(strcmp(argv[i], "-test") == 0){
               test = true;
          }else if(strcmp(argv[i], "-suite") == 0){
               test = true;
               suite = true;
          }else if(strcmp(argv[i], "-show") == 0){
               show_suite = true;
          }else if(strcmp(argv[i], "-map") == 0){
               int next = i + 1;
               if(next >= argc) continue;
               map_number = atoi(argv[next]);
               first_map_number = map_number;
          }else if(strcmp(argv[i], "-count") == 0){
               int next = i + 1;
               if(next >= argc) continue;
               map_count = atoi(argv[next]);
          }
     }

     const char* log_path = "bryte.log";
     if(!Log_t::create(log_path)){
          fprintf(stderr, "failed to create log file: '%s'\n", log_path);
          return -1;
     }

     if(test && !load_map_filepath && !suite){
          LOG("cannot test without specifying a map to load\n");
          return 1;
     }

     int window_width = 800;
     int window_height = 800;
     SDL_Window* window = nullptr;
     SDL_GLContext opengl_context = 0;
     GLuint theme_texture = 0;
     GLuint player_texture = 0;
     GLuint arrow_texture = 0;

     if(!suite || show_suite){
          if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0){
               return 1;
          }

          SDL_DisplayMode display_mode;
          if(SDL_GetCurrentDisplayMode(0, &display_mode) != 0){
               return 1;
          }

          LOG("Create window: %d, %d\n", window_width, window_height);
          window = SDL_CreateWindow("bryte", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_OPENGL);
          if(!window) return 1;

          opengl_context = SDL_GL_CreateContext(window);

          SDL_GL_SetSwapInterval(SDL_TRUE);
          glViewport(0, 0, window_width, window_height);
          glClearColor(0.0, 0.0, 0.0, 1.0);
          glEnable(GL_TEXTURE_2D);
          glViewport(0, 0, window_width, window_height);
          glMatrixMode(GL_PROJECTION);
          glLoadIdentity();
          glOrtho(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);
          glMatrixMode(GL_MODELVIEW);
          glLoadIdentity();
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          glBlendEquation(GL_FUNC_ADD);

          theme_texture = transparent_texture_from_file("content/theme.bmp");
          if(theme_texture == 0) return 1;
          player_texture = transparent_texture_from_file("content/player.bmp");
          if(player_texture == 0) return 1;
          arrow_texture = transparent_texture_from_file("content/arrow.bmp");
          if(arrow_texture == 0) return 1;
     }

#if 0 // TODO: do we want this in the future
     Rect_t rooms[2];
     {
          rooms[0].left = 0;
          rooms[0].bottom = 0;
          rooms[0].top = 16;
          rooms[0].right = 16;

          rooms[1].left = 17;
          rooms[1].bottom = 5;
          rooms[1].top = 15;
          rooms[1].right = 27;
     }
#endif

     FILE* demo_file = nullptr;
     DemoEntry_t demo_entry {};
     switch(demo_mode){
     default:
          break;
     case DEMO_MODE_RECORD:
          demo_file = fopen(demo_filepath, "w");
          if(!demo_file){
               LOG("failed to open demo file: %s\n", demo_filepath);
               return 1;
          }
          // TODO: write header
          break;
     case DEMO_MODE_PLAY:
          demo_file = fopen(demo_filepath, "r");
          if(!demo_file){
               LOG("failed to open demo file: %s\n", demo_filepath);
               return 1;
          }
          LOG("playing demo %s\n", demo_filepath);
          // TODO: read header
          demo_entry_get(&demo_entry, demo_file);
          break;
     }

     TileMap_t tilemap = {};
     ObjectArray_t<Block_t> block_array = {};
     ObjectArray_t<Interactive_t> interactive_array = {};
     ArrowArray_t arrow_array = {};
     Coord_t player_start {2, 8};

     if(load_map_filepath){
          if(!load_map(load_map_filepath, &player_start, &tilemap, &block_array, &interactive_array)){
               return 1;
          }
     }else if(suite){
          if(!load_map_number(map_number, &player_start, &tilemap, &block_array, &interactive_array)){
               return 1;
          }

          demo_mode = DEMO_MODE_PLAY;
          demo_file = load_demo_number(map_number, &demo_filepath);
          if(!demo_file){
               LOG("missing map %d corresponding demo.\n", map_number);
               return 1;
          }
          LOG("testing demo %s\n", demo_filepath);
          demo_entry_get(&demo_entry, demo_file);
     }else if(map_number){
          if(!load_map_number(map_number, &player_start, &tilemap, &block_array, &interactive_array)){
               return 1;
          }
     }else{
          init(&tilemap, ROOM_TILE_SIZE, ROOM_TILE_SIZE);

          for(S16 i = 0; i < tilemap.width; i++){
               tilemap.tiles[0][i].id = 33;
               tilemap.tiles[1][i].id = 17;
               tilemap.tiles[tilemap.height - 1][i].id = 16;
               tilemap.tiles[tilemap.height - 2][i].id = 32;
          }

          for(S16 i = 0; i < tilemap.height; i++){
               tilemap.tiles[i][0].id = 18;
               tilemap.tiles[i][1].id = 19;
               tilemap.tiles[i][tilemap.width - 2].id = 34;
               tilemap.tiles[i][tilemap.height - 1].id = 35;
          }

          tilemap.tiles[0][0].id = 36;
          tilemap.tiles[0][1].id = 37;
          tilemap.tiles[1][0].id = 20;
          tilemap.tiles[1][1].id = 21;

          tilemap.tiles[16][0].id = 22;
          tilemap.tiles[16][1].id = 23;
          tilemap.tiles[15][0].id = 38;
          tilemap.tiles[15][1].id = 39;

          tilemap.tiles[15][15].id = 40;
          tilemap.tiles[15][16].id = 41;
          tilemap.tiles[16][15].id = 24;
          tilemap.tiles[16][16].id = 25;

          tilemap.tiles[0][15].id = 42;
          tilemap.tiles[0][16].id = 43;
          tilemap.tiles[1][15].id = 26;
          tilemap.tiles[1][16].id = 27;
          if(!init(&interactive_array, 1)){
               return 1;
          }
          interactive_array.elements[0].coord.x = -1;
          interactive_array.elements[0].coord.y = -1;

          if(!init(&block_array, 1)){
               return 1;
          }
          block_array.elements[0].pos = coord_to_pos(Coord_t{-1, -1});
     }

     Player_t player;
     QuadTreeNode_t<Interactive_t>* interactive_quad_tree = nullptr;
     QuadTreeNode_t<Block_t>* block_quad_tree = nullptr;

     reset_map(&player, player_start, &interactive_array, &interactive_quad_tree);

     Undo_t undo = {};
     init(&undo, 4 * 1024 * 1024, tilemap.width, tilemap.height, block_array.count, interactive_array.count);

     undo_snapshot(&undo, &player, &tilemap, &block_array, &interactive_array);

     bool quit = false;

     Vec_t user_movement = {};
     PlayerAction_t player_action {};

     auto last_time = system_clock::now();
     auto current_time = last_time;

     Position_t camera = coord_to_pos(Coord_t{8, 8});

     Block_t* last_block_pushed = nullptr;
     Direction_t last_block_pushed_direction = DIRECTION_LEFT;
     Block_t* block_to_push = nullptr;

     // bool left_click_down = false;
     Vec_t mouse_screen = {}; // 0.0f to 1.0f
     Position_t mouse_world = {};
     bool ctrl_down;

     Editor_t editor;
     init(&editor);

     S64 frame_count = 0;
     F32 dt = 0.0f;

     while(!quit){
          if(!suite || show_suite){
               current_time = system_clock::now();
               duration<double> elapsed_seconds = current_time - last_time;
               dt = (F64)(elapsed_seconds.count());
               if(dt < 0.0166666f) continue; // limit 60 fps
          }

          // TODO: consider 30fps as minimum for random noobs computers
          if(demo_mode) dt = 0.0166666f; // the game always runs as if a 60th of a frame has occurred.

          quad_tree_free(block_quad_tree);
          block_quad_tree = quad_tree_build(&block_array);

          frame_count++;

          last_time = current_time;

          last_block_pushed = block_to_push;
          block_to_push = nullptr;

          player_action.last_activate = player_action.activate;
          player_action.reface = false;

          if(demo_mode == DEMO_MODE_PLAY){
               bool end_of_demo = false;
               if(demo_entry.player_action_type == PLAYER_ACTION_TYPE_END_DEMO){
                    end_of_demo = (frame_count == demo_entry.frame);
               }else{
                    while(frame_count == demo_entry.frame && !feof(demo_file)){
                         player_action_perform(&player_action, &player, demo_entry.player_action_type, demo_mode,
                                               demo_file, frame_count);
                         demo_entry_get(&demo_entry, demo_file);
                    }
               }

               if(end_of_demo){
                    if(test){
                         TileMap_t check_tilemap = {};
                         ObjectArray_t<Block_t> check_block_array = {};
                         ObjectArray_t<Interactive_t> check_interactive_array = {};
                         Coord_t check_player_start;
                         Pixel_t check_player_pixel;
                         if(!load_map_from_file(demo_file, &check_player_start, &check_tilemap, &check_block_array, &check_interactive_array, demo_filepath)){
                              LOG("failed to load map state from end of file\n");
                              demo_mode = DEMO_MODE_NONE;
                         }else{
                              bool test_passed = true;

                              fread(&check_player_pixel, sizeof(check_player_pixel), 1, demo_file);
                              if(check_tilemap.width != tilemap.width){
                                   LOG_MISMATCH("tilemap width", "%d", check_tilemap.width, tilemap.width);
                              }else if(check_tilemap.height != tilemap.height){
                                   LOG_MISMATCH("tilemap height", "%d", check_tilemap.height, tilemap.height);
                              }else{
                                   for(S16 j = 0; j < check_tilemap.height; j++){
                                        for(S16 i = 0; i < check_tilemap.width; i++){
                                             if(check_tilemap.tiles[j][i].flags != tilemap.tiles[j][i].flags){
                                                  char name[64];
                                                  snprintf(name, 64, "tile %d, %d flags", i, j);
                                                  LOG_MISMATCH(name, "%d", check_tilemap.tiles[j][i].flags, tilemap.tiles[j][i].flags);
                                             }
                                        }
                                   }
                              }

                              if(check_player_pixel.x != player.pos.pixel.x){
                                   LOG_MISMATCH("player pixel x", "%d", check_player_pixel.x, player.pos.pixel.x);
                              }

                              if(check_player_pixel.y != player.pos.pixel.y){
                                   LOG_MISMATCH("player pixel y", "%d", check_player_pixel.y, player.pos.pixel.y);
                              }

                              if(check_block_array.count != block_array.count){
                                   LOG_MISMATCH("block count", "%d", check_block_array.count, block_array.count);
                              }else{
                                   for(S16 i = 0; i < check_block_array.count; i++){
                                        // TODO: consider checking other things
                                        Block_t* check_block = check_block_array.elements + i;
                                        Block_t* block = block_array.elements + i;
                                        if(check_block->pos.pixel.x != block->pos.pixel.x){
                                             char name[64];
                                             snprintf(name, 64, "block %d pos x", i);
                                             LOG_MISMATCH(name, "%d", check_block->pos.pixel.x, block->pos.pixel.x);
                                        }

                                        if(check_block->pos.pixel.y != block->pos.pixel.y){
                                             char name[64];
                                             snprintf(name, 64, "block %d pos y", i);
                                             LOG_MISMATCH(name, "%d", check_block->pos.pixel.y, block->pos.pixel.y);
                                        }

                                        if(check_block->pos.z != block->pos.z){
                                             char name[64];
                                             snprintf(name, 64, "block %d pos z", i);
                                             LOG_MISMATCH(name, "%d", check_block->pos.z, block->pos.z);
                                        }

                                        if(check_block->element != block->element){
                                             char name[64];
                                             snprintf(name, 64, "block %d element", i);
                                             LOG_MISMATCH(name, "%d", check_block->element, block->element);
                                        }
                                   }
                              }

                              if(check_interactive_array.count != interactive_array.count){
                                   LOG_MISMATCH("interactive count", "%d", check_interactive_array.count, interactive_array.count);
                              }else{
                                   for(S16 i = 0; i < check_interactive_array.count; i++){
                                        // TODO: consider checking other things
                                        Interactive_t* check_interactive = check_interactive_array.elements + i;
                                        Interactive_t* interactive = interactive_array.elements + i;

                                        if(check_interactive->type != interactive->type){
                                             LOG_MISMATCH("interactive type", "%d", check_interactive->type, interactive->type);
                                        }else{
                                             switch(check_interactive->type){
                                             default:
                                                  break;
                                             case INTERACTIVE_TYPE_PRESSURE_PLATE:
                                                  if(check_interactive->pressure_plate.down != interactive->pressure_plate.down){
                                                       char name[64];
                                                       snprintf(name, 64, "interactive at %d, %d pressure plate down",
                                                                interactive->coord.x, interactive->coord.y);
                                                       LOG_MISMATCH(name, "%d", check_interactive->pressure_plate.down,
                                                                    interactive->pressure_plate.down);
                                                  }
                                                  break;
                                             case INTERACTIVE_TYPE_ICE_DETECTOR:
                                             case INTERACTIVE_TYPE_LIGHT_DETECTOR:
                                                  if(check_interactive->detector.on != interactive->detector.on){
                                                       char name[64];
                                                       snprintf(name, 64, "interactive at %d, %d detector on",
                                                                interactive->coord.x, interactive->coord.y);
                                                       LOG_MISMATCH(name, "%d", check_interactive->detector.on,
                                                                    interactive->detector.on);
                                                  }
                                                  break;
                                             case INTERACTIVE_TYPE_POPUP:
                                                  if(check_interactive->popup.iced != interactive->popup.iced){
                                                       char name[64];
                                                       snprintf(name, 64, "interactive at %d, %d popup iced",
                                                                interactive->coord.x, interactive->coord.y);
                                                       LOG_MISMATCH(name, "%d", check_interactive->popup.iced,
                                                                    interactive->popup.iced);
                                                  }
                                                  if(check_interactive->popup.lift.up != interactive->popup.lift.up){
                                                       char name[64];
                                                       snprintf(name, 64, "interactive at %d, %d popup lift up",
                                                                interactive->coord.x, interactive->coord.y);
                                                       LOG_MISMATCH(name, "%d", check_interactive->popup.lift.up,
                                                                    interactive->popup.lift.up);
                                                  }
                                                  break;
                                             case INTERACTIVE_TYPE_DOOR:
                                                  if(check_interactive->door.lift.up != interactive->door.lift.up){
                                                       char name[64];
                                                       snprintf(name, 64, "interactive at %d, %d door lift up",
                                                                interactive->coord.x, interactive->coord.y);
                                                       LOG_MISMATCH(name, "%d", check_interactive->door.lift.up,
                                                                    interactive->door.lift.up);
                                                  }
                                                  break;
                                             case INTERACTIVE_TYPE_PORTAL:
                                                  break;
                                             }
                                        }
                                   }
                              }

                              if(!test_passed){
                                   LOG("test failed\n");
                                   demo_mode = DEMO_MODE_NONE;
                                   if(suite && !show_suite) return 1;
                              }else if(suite){
                                   map_number++;
                                   S16 maps_tested = map_number - first_map_number;
                                   if(map_count > 0 && maps_tested >= map_count){
                                        LOG("Done Testing %d maps.\n", map_count);
                                        return 0;
                                   }
                                   if(load_map_number(map_number, &player_start, &tilemap, &block_array, &interactive_array)){
                                        reset_map(&player, player_start, &interactive_array, &interactive_quad_tree);

                                        // reset some vars
                                        player_action = {};
                                        last_block_pushed = nullptr;
                                        last_block_pushed_direction = DIRECTION_LEFT;
                                        block_to_push = nullptr;

                                        fclose(demo_file);
                                        demo_file = load_demo_number(map_number, &demo_filepath);
                                        if(demo_file){
                                             LOG("testing demo %s\n", demo_filepath);
                                             demo_entry_get(&demo_entry, demo_file);
                                             frame_count = 0;
                                             continue; // reset to the top of the loop
                                        }else{
                                             LOG("missing map %d corresponding demo.\n", map_number);
                                             return 1;
                                        }
                                   }else{
                                        LOG("Done Testing %d maps.\n", maps_tested);
                                        return 0;
                                   }
                              }else{
                                   LOG("test passed\n");
                              }
                         }
                    }else{
                         demo_mode = DEMO_MODE_NONE;
                         LOG("end of demo %s\n", demo_filepath);
                    }
               }
          }

          SDL_Event sdl_event;
          while(SDL_PollEvent(&sdl_event)){
               switch(sdl_event.type){
               default:
                    break;
               case SDL_KEYDOWN:
                    switch(sdl_event.key.keysym.scancode){
                    default:
                         break;
                    case SDL_SCANCODE_ESCAPE:
                         quit = true;
                         break;
                    case SDL_SCANCODE_LEFT:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              editor.selection_start.x--;
                              editor.selection_end.x--;
                         }else{
                              player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_MOVE_LEFT_START, demo_mode,
                                                    demo_file, frame_count);
                         }
                         break;
                    case SDL_SCANCODE_RIGHT:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              editor.selection_start.x++;
                              editor.selection_end.x++;
                         }else{
                              player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_MOVE_RIGHT_START, demo_mode,
                                                    demo_file, frame_count);
                         }
                         break;
                    case SDL_SCANCODE_UP:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              editor.selection_start.y++;
                              editor.selection_end.y++;
                         }else{
                              player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_MOVE_UP_START, demo_mode,
                                                    demo_file, frame_count);
                         }
                         break;
                    case SDL_SCANCODE_DOWN:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              editor.selection_start.y--;
                              editor.selection_end.y--;
                         }else{
                              player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_MOVE_DOWN_START, demo_mode,
                                                    demo_file, frame_count);
                         }
                         break;
                    case SDL_SCANCODE_E:
                         player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_ACTIVATE_START, demo_mode,
                                               demo_file, frame_count);
                         break;
                    case SDL_SCANCODE_SPACE:
                         player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_SHOOT_START, demo_mode,
                                               demo_file, frame_count);
                         break;
                    case SDL_SCANCODE_L:
                         if(load_map_number(map_number, &player_start, &tilemap, &block_array, &interactive_array)){
                              reset_map(&player, player_start, &interactive_array, &interactive_quad_tree);
                         }
                         break;
                    case SDL_SCANCODE_LEFTBRACKET:
                         map_number--;
                         if(load_map_number(map_number, &player_start, &tilemap, &block_array, &interactive_array)){
                              reset_map(&player, player_start, &interactive_array, &interactive_quad_tree);
                         }else{
                              map_number++;
                         }
                         break;
                    case SDL_SCANCODE_RIGHTBRACKET:
                         map_number++;
                         if(load_map_number(map_number, &player_start, &tilemap, &block_array, &interactive_array)){
                              reset_map(&player, player_start, &interactive_array, &interactive_quad_tree);
                         }else{
                              map_number--;
                         }
                         break;
                    case SDL_SCANCODE_V:
                    {
                         char filepath[64];
                         snprintf(filepath, 64, "content/%03d.bm", map_number);
                         save_map(filepath, player_start, &tilemap, &block_array, &interactive_array);
                    } break;
                    case SDL_SCANCODE_U:
                         undo_revert(&undo, &player, &tilemap, &block_array, &interactive_array);
                         break;
                    case SDL_SCANCODE_N:
                    {
                         Tile_t* tile = tilemap_get_tile(&tilemap, mouse_select_world(mouse_screen, camera));
                         if(tile){
                              if(tile->flags & TILE_FLAG_WIRE_CLUSTER_LEFT) tile->flags |= TILE_FLAG_WIRE_CLUSTER_LEFT_ON;
                              if(tile->flags & TILE_FLAG_WIRE_CLUSTER_MID) tile->flags |= TILE_FLAG_WIRE_CLUSTER_MID_ON;
                              if(tile->flags & TILE_FLAG_WIRE_CLUSTER_RIGHT) tile->flags |= TILE_FLAG_WIRE_CLUSTER_RIGHT_ON;
                         }
                    } break;
                    // TODO: #ifdef DEBUG
                    case SDL_SCANCODE_GRAVE:
                         if(editor.mode == EDITOR_MODE_OFF){
                              editor.mode = EDITOR_MODE_CATEGORY_SELECT;
                         }else{
                              editor.mode = EDITOR_MODE_OFF;
                              editor.selection_start = {};
                              editor.selection_end = {};
                         }
                         break;
                    case SDL_SCANCODE_TAB:
                         if(editor.mode == EDITOR_MODE_STAMP_SELECT){
                              editor.mode = EDITOR_MODE_STAMP_HIDE;
                         }else{
                              editor.mode = EDITOR_MODE_STAMP_SELECT;
                         }
                         break;
                    case SDL_SCANCODE_RETURN:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              // clear coords below stamp
                              Rect_t selection_bounds = editor_selection_bounds(&editor);
                              for(S16 j = selection_bounds.bottom; j <= selection_bounds.top; j++){
                                   for(S16 i = selection_bounds.left; i <= selection_bounds.right; i++){
                                        Coord_t coord {i, j};
                                        coord_clear(coord, &tilemap, &interactive_array, interactive_quad_tree, &block_array);
                                   }
                              }

                              for(int i = 0; i < editor.selection.count; i++){
                                   Coord_t coord = editor.selection_start + editor.selection.elements[i].offset;
                                   apply_stamp(editor.selection.elements + i, coord,
                                               &tilemap, &block_array, &interactive_array, &interactive_quad_tree, ctrl_down);
                              }

                              editor.mode = EDITOR_MODE_CATEGORY_SELECT;
                         }
                         break;
                    case SDL_SCANCODE_T:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              // TODO: compress selection sort
                              if(editor.selection_start.x > editor.selection_end.x) SWAP(editor.selection_start.x, editor.selection_end.x);
                              if(editor.selection_start.y > editor.selection_end.y) SWAP(editor.selection_start.y, editor.selection_end.y);

                              S16 height_offset = (editor.selection_end.y - editor.selection_start.y) - 1;

                              // perform rotation on each offset
                              for(S16 i = 0; i < editor.selection.count; i++){
                                   auto* stamp = editor.selection.elements + i;
                                   Coord_t rot {stamp->offset.y, (S16)(-stamp->offset.x + height_offset)};
                                   stamp->offset = rot;
                              }
                         }
                         break;
                    case SDL_SCANCODE_X:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              destroy(&editor.clipboard);
                              shallow_copy(&editor.selection, &editor.clipboard);
                              editor.mode = EDITOR_MODE_CATEGORY_SELECT;
                         }
                         break;
                    case SDL_SCANCODE_P:
                         if(editor.mode == EDITOR_MODE_CATEGORY_SELECT && editor.clipboard.count){
                              destroy(&editor.selection);
                              shallow_copy(&editor.clipboard, &editor.selection);
                              editor.mode = EDITOR_MODE_SELECTION_MANIPULATION;
                         }
                         break;
                    case SDL_SCANCODE_M:
                         if(editor.mode == EDITOR_MODE_CATEGORY_SELECT){
                              player_start = mouse_select_world(mouse_screen, camera);
                         }
                         break;
                    case SDL_SCANCODE_LCTRL:
                         ctrl_down = true;
                         break;
                    }
                    break;
               case SDL_KEYUP:
                    switch(sdl_event.key.keysym.scancode){
                    default:
                         break;
                    case SDL_SCANCODE_ESCAPE:
                         quit = true;
                         break;
                    case SDL_SCANCODE_LEFT:
                         player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_MOVE_LEFT_STOP, demo_mode,
                                               demo_file, frame_count);
                         break;
                    case SDL_SCANCODE_RIGHT:
                         player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_MOVE_RIGHT_STOP, demo_mode,
                                               demo_file, frame_count);
                         break;
                    case SDL_SCANCODE_UP:
                         player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_MOVE_UP_STOP, demo_mode,
                                               demo_file, frame_count);
                         break;
                    case SDL_SCANCODE_DOWN:
                         player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_MOVE_DOWN_STOP, demo_mode,
                                               demo_file, frame_count);
                         break;
                    case SDL_SCANCODE_E:
                         player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_ACTIVATE_STOP, demo_mode,
                                               demo_file, frame_count);
                         break;
                    case SDL_SCANCODE_SPACE:
                         player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_SHOOT_STOP, demo_mode,
                                               demo_file, frame_count);
                         break;
                    case SDL_SCANCODE_LCTRL:
                         ctrl_down = false;
                         break;
                    }
                    break;
               case SDL_MOUSEBUTTONDOWN:
                    switch(sdl_event.button.button){
                    default:
                         break;
                    case SDL_BUTTON_LEFT:
                         // left_click_down = true;

                         switch(editor.mode){
                         default:
                              break;
                         case EDITOR_MODE_OFF:
                              break;
                         case EDITOR_MODE_CATEGORY_SELECT:
                         case EDITOR_MODE_SELECTION_MANIPULATION:
                         {
                              Coord_t mouse_coord = vec_to_coord(mouse_screen);
                              S32 select_index = (mouse_coord.y * ROOM_TILE_SIZE) + mouse_coord.x;
                              if(select_index < EDITOR_CATEGORY_COUNT){
                                   editor.mode = EDITOR_MODE_STAMP_SELECT;
                                   editor.category = select_index;
                                   editor.stamp = 0;
                              }else{
                                   editor.mode = EDITOR_MODE_CREATE_SELECTION;
                                   editor.selection_start = mouse_select_world(mouse_screen, camera);
                                   editor.selection_end = editor.selection_start;
                              }
                         } break;
                         case EDITOR_MODE_STAMP_SELECT:
                         case EDITOR_MODE_STAMP_HIDE:
                         {
                              S32 select_index = mouse_select_stamp_index(vec_to_coord(mouse_screen),
                                                                          editor.category_array.elements + editor.category);
                              if(editor.mode != EDITOR_MODE_STAMP_HIDE && select_index < editor.category_array.elements[editor.category].count && select_index >= 0){
                                   editor.stamp = select_index;
                              }else{
                                   Coord_t select_coord = mouse_select_world(mouse_screen, camera);
                                   auto* stamp_array = editor.category_array.elements[editor.category].elements + editor.stamp;
                                   for(S16 s = 0; s < stamp_array->count; s++){
                                        auto* stamp = stamp_array->elements + s;
                                        apply_stamp(stamp, select_coord + stamp->offset,
                                                    &tilemap, &block_array, &interactive_array, &interactive_quad_tree, ctrl_down);
                                   }

                                   quad_tree_free(block_quad_tree);
                                   block_quad_tree = quad_tree_build(&block_array);
                              }
                         } break;
                         }
                         break;
                    case SDL_BUTTON_RIGHT:
                         switch(editor.mode){
                         default:
                              break;
                         case EDITOR_MODE_CATEGORY_SELECT:
                              coord_clear(mouse_select_world(mouse_screen, camera), &tilemap, &interactive_array,
                                          interactive_quad_tree, &block_array);
                              break;
                         case EDITOR_MODE_STAMP_SELECT:
                         case EDITOR_MODE_STAMP_HIDE:
                         {
                              Coord_t start = mouse_select_world(mouse_screen, camera);
                              Coord_t end = start + stamp_array_dimensions(editor.category_array.elements[editor.category].elements + editor.stamp);
                              for(S16 j = start.y; j < end.y; j++){
                                   for(S16 i = start.x; i < end.x; i++){
                                        Coord_t coord {i, j};
                                        coord_clear(coord, &tilemap, &interactive_array, interactive_quad_tree, &block_array);
                                   }
                              }
                         } break;
                         case EDITOR_MODE_SELECTION_MANIPULATION:
                         {
                              Rect_t selection_bounds = editor_selection_bounds(&editor);
                              for(S16 j = selection_bounds.bottom; j <= selection_bounds.top; j++){
                                   for(S16 i = selection_bounds.left; i <= selection_bounds.right; i++){
                                        Coord_t coord {i, j};
                                        coord_clear(coord, &tilemap, &interactive_array, interactive_quad_tree, &block_array);
                                   }
                              }
                         } break;
                         }
                         break;
                    }
                    break;
               case SDL_MOUSEBUTTONUP:
                    switch(sdl_event.button.button){
                    default:
                         break;
                    case SDL_BUTTON_LEFT:
                         // left_click_down = false;
                         switch(editor.mode){
                         default:
                              break;
                         case EDITOR_MODE_CREATE_SELECTION:
                         {
                              editor.selection_end = mouse_select_world(mouse_screen, camera);

                              // sort selection range
                              if(editor.selection_start.x > editor.selection_end.x) SWAP(editor.selection_start.x, editor.selection_end.x);
                              if(editor.selection_start.y > editor.selection_end.y) SWAP(editor.selection_start.y, editor.selection_end.y);

                              destroy(&editor.selection);

                              S16 stamp_count = (((editor.selection_end.x - editor.selection_start.x) + 1) * ((editor.selection_end.y - editor.selection_start.y) + 1)) * 2;
                              init(&editor.selection, stamp_count);
                              S16 stamp_index = 0;
                              for(S16 j = editor.selection_start.y; j <= editor.selection_end.y; j++){
                                   for(S16 i = editor.selection_start.x; i <= editor.selection_end.x; i++){
                                        Coord_t coord = {i, j};
                                        Coord_t offset = coord - editor.selection_start;

                                        // tile id
                                        Tile_t* tile = tilemap_get_tile(&tilemap, coord);
                                        editor.selection.elements[stamp_index].type = STAMP_TYPE_TILE_ID;
                                        editor.selection.elements[stamp_index].tile_id = tile->id;
                                        editor.selection.elements[stamp_index].offset = offset;
                                        stamp_index++;

                                        // tile flags
                                        editor.selection.elements[stamp_index].type = STAMP_TYPE_TILE_FLAGS;
                                        editor.selection.elements[stamp_index].tile_flags = tile->flags;
                                        editor.selection.elements[stamp_index].offset = offset;
                                        stamp_index++;

                                        // interactive
                                        auto* interactive = quad_tree_interactive_find_at(interactive_quad_tree, coord);
                                        if(interactive){
                                             resize(&editor.selection, editor.selection.count + 1);
                                             auto* stamp = editor.selection.elements + (editor.selection.count - 1);
                                             stamp->type = STAMP_TYPE_INTERACTIVE;
                                             stamp->interactive = *interactive;
                                             stamp->offset = offset;
                                        }

                                        for(S16 b = 0; b < block_array.count; b++){
                                             auto* block = block_array.elements + b;
                                             if(pos_to_coord(block->pos) == coord){
                                                  resize(&editor.selection, editor.selection.count + 1);
                                                  auto* stamp = editor.selection.elements + (editor.selection.count - 1);
                                                  stamp->type = STAMP_TYPE_BLOCK;
                                                  stamp->block.face = block->face;
                                                  stamp->block.element = block->element;
                                                  stamp->offset = offset;
                                             }
                                        }
                                   }
                              }

                              editor.mode = EDITOR_MODE_SELECTION_MANIPULATION;
                         } break;
                         }
                         break;
                    }
                    break;
               case SDL_MOUSEMOTION:
                    mouse_screen = Vec_t{((F32)(sdl_event.button.x) / (F32)(window_width)),
                                         1.0f - ((F32)(sdl_event.button.y) / (F32)(window_height))};
                    mouse_world = vec_to_pos(mouse_screen);
                    switch(editor.mode){
                    default:
                         break;
                    case EDITOR_MODE_CREATE_SELECTION:
                         if(editor.selection_start.x >= 0 && editor.selection_start.y >= 0){
                              editor.selection_end = pos_to_coord(mouse_world);
                         }
                         break;
                    }
                    break;
               }
          }

          // reset base light
          for(S16 j = 0; j < tilemap.height; j++){
               for(S16 i = 0; i < tilemap.width; i++){
                    tilemap.tiles[j][i].light = BASE_LIGHT;
               }
          }

          // update interactives
          for(S16 i = 0; i < interactive_array.count; i++){
               Interactive_t* interactive = interactive_array.elements + i;
               if(interactive->type == INTERACTIVE_TYPE_POPUP){
                    lift_update(&interactive->popup.lift, POPUP_TICK_DELAY, dt, 1, HEIGHT_INTERVAL + 1);
               }else if(interactive->type == INTERACTIVE_TYPE_DOOR){
                    lift_update(&interactive->door.lift, POPUP_TICK_DELAY, dt, 0, DOOR_MAX_HEIGHT);
               }else if(interactive->type == INTERACTIVE_TYPE_PRESSURE_PLATE){
                    bool should_be_down = false;
                    Coord_t player_coord = pos_to_coord(player.pos);
                    if(interactive->coord == player_coord){
                         should_be_down = true;
                    }else{
                         Tile_t* tile = tilemap_get_tile(&tilemap, interactive->coord);
                         if(tile){
                              if(!tile_is_iced(tile)){
                                   Pixel_t center = coord_to_pixel(interactive->coord) + Pixel_t{HALF_TILE_SIZE_IN_PIXELS, HALF_TILE_SIZE_IN_PIXELS};
                                   Rect_t rect = {};
                                   rect.left = center.x - (2 * TILE_SIZE_IN_PIXELS);
                                   rect.right = center.x + (2 * TILE_SIZE_IN_PIXELS);
                                   rect.bottom = center.y - (2 * TILE_SIZE_IN_PIXELS);
                                   rect.top = center.y + (2 * TILE_SIZE_IN_PIXELS);

                                   S16 block_count = 0;
                                   Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
                                   quad_tree_find_in(block_quad_tree, rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

                                   for(S16 b = 0; b < block_count; b++){
                                        Coord_t block_coord = block_get_coord(blocks[b]);
                                        if(interactive->coord == block_coord){
                                             should_be_down = true;
                                             break;
                                        }
                                   }
                              }
                         }
                    }

                    if(should_be_down != interactive->pressure_plate.down){
                         activate(&tilemap, interactive_quad_tree, interactive->coord);
                         interactive->pressure_plate.down = should_be_down;
                    }
               }
          }

          // update arrows
          for(S16 i = 0; i < ARROW_ARRAY_MAX; i++){
               Arrow_t* arrow = arrow_array.arrows + i;
               if(!arrow->alive) continue;

               Coord_t pre_move_coord = pixel_to_coord(arrow->pos.pixel + Pixel_t{0, arrow->pos.z});

               if(arrow->element == ELEMENT_FIRE){
                    illuminate(pre_move_coord, 255 - LIGHT_DECAY, &tilemap, block_quad_tree);
               }

               if(arrow->stuck_time > 0.0f){
                    arrow->stuck_time += dt;
                    if(arrow->stuck_time > ARROW_DISINTEGRATE_DELAY){
                         arrow->alive = false;
                    }
                    continue;
               }

               F32 arrow_friction = 0.9999f;

               if(arrow->pos.z > 0){
                    // TODO: fall based on the timer !
                    arrow->fall_time += dt;
                    if(arrow->fall_time > ARROW_FALL_DELAY){
                         arrow->fall_time -= ARROW_FALL_DELAY;
                         arrow->pos.z--;
                    }
               }else{
                    arrow_friction = 0.9f;
               }

               Vec_t direction = {};
               switch(arrow->face){
               default:
                    break;
               case DIRECTION_LEFT:
                    direction.x = -1;
                    break;
               case DIRECTION_RIGHT:
                    direction.x = 1;
                    break;
               case DIRECTION_DOWN:
                    direction.y = -1;
                    break;
               case DIRECTION_UP:
                    direction.y = 1;
                    break;
               }

               arrow->pos += (direction * dt * arrow->vel);
               arrow->vel *= arrow_friction;
               Coord_t post_move_coord = pixel_to_coord(arrow->pos.pixel + Pixel_t{0, arrow->pos.z});

               Rect_t coord_rect {(S16)(arrow->pos.pixel.x - TILE_SIZE_IN_PIXELS),
                                  (S16)(arrow->pos.pixel.y - TILE_SIZE_IN_PIXELS),
                                  (S16)(arrow->pos.pixel.x + TILE_SIZE_IN_PIXELS),
                                  (S16)(arrow->pos.pixel.y + TILE_SIZE_IN_PIXELS)};

               S16 block_count = 0;
               Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
               quad_tree_find_in(block_quad_tree, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);
               for(S16 b = 0; b < block_count; b++){
                    // blocks on the coordinate and on the ground block light
                    Rect_t block_rect = {(S16)(blocks[b]->pos.pixel.x), (S16)(blocks[b]->pos.pixel.y),
                                         (S16)(blocks[b]->pos.pixel.x + TILE_SIZE_IN_PIXELS),
                                         (S16)(blocks[b]->pos.pixel.y + TILE_SIZE_IN_PIXELS)};
                    S16 block_index = blocks[b] - block_array.elements;
                    if(pixel_in_rect(arrow->pos.pixel, block_rect) && arrow->element_from_block != block_index){
                         arrow->element_from_block = block_index;
                         if(arrow->element != blocks[b]->element){
                              Element_t arrow_element = arrow->element;
                              arrow->element = transition_element(arrow->element, blocks[b]->element);
                              if(arrow_element){
                                   blocks[b]->element = transition_element(blocks[b]->element, arrow_element);
                              }
                         }
                         break;
                    }
               }

               if(block_count == 0){
                    arrow->element_from_block = -1;
               }

               if(pre_move_coord != post_move_coord){
                    Tile_t* tile = tilemap_get_tile(&tilemap, post_move_coord);
                    if(tile_is_solid(tile)){
                         arrow->stuck_time = dt;
                    }

                    // catch or give elements
                    if(arrow->element == ELEMENT_FIRE){
                         melt_ice(post_move_coord, 0, &tilemap, interactive_quad_tree, block_quad_tree);
                    }else if(arrow->element == ELEMENT_ICE){
                         spread_ice(post_move_coord, 0, &tilemap, interactive_quad_tree, block_quad_tree);
                    }

                    Interactive_t* interactive = quad_tree_interactive_find_at(interactive_quad_tree, post_move_coord);
                    if(interactive){
                         if(interactive->type == INTERACTIVE_TYPE_LEVER){
                              if(arrow->pos.z >= HEIGHT_INTERVAL){
                                   activate(&tilemap, interactive_quad_tree, post_move_coord);
                              }else{
                                   arrow->stuck_time = dt;
                              }
                         }else if(interactive->type == INTERACTIVE_TYPE_DOOR){
                              if(interactive->door.lift.ticks < arrow->pos.z){
                                   arrow->stuck_time = dt;
                              }
                         }
                    }
               }
          }

          // update player
          user_movement = vec_zero();

          if(player_action.move_left){
               user_movement += Vec_t{-1, 0};
               if(player_action.reface) player.face = DIRECTION_LEFT;
          }

          if(player_action.move_right){
               user_movement += Vec_t{1, 0};
               if(player_action.reface) player.face = DIRECTION_RIGHT;
          }

          if(player_action.move_up){
               user_movement += Vec_t{0, 1};
               if(player_action.reface) player.face = DIRECTION_UP;
          }

          if(player_action.move_down){
               user_movement += Vec_t{0, -1};
               if(player_action.reface) player.face = DIRECTION_DOWN;
          }

          if(player_action.activate && !player_action.last_activate){
               activate(&tilemap, interactive_quad_tree, pos_to_coord(player.pos) + player.face);
          }

          if(player.has_bow && player_action.shoot && player.bow_draw_time < PLAYER_BOW_DRAW_DELAY){
               player.bow_draw_time += dt;
          }else if(!player_action.shoot){
               if(player.bow_draw_time >= PLAYER_BOW_DRAW_DELAY){
                    Position_t arrow_pos = player.pos;
                    switch(player.face){
                    default:
                         break;
                    case DIRECTION_LEFT:
                         arrow_pos.pixel.y -= 2;
                         arrow_pos.pixel.x -= 8;
                         break;
                    case DIRECTION_RIGHT:
                         arrow_pos.pixel.y -= 2;
                         arrow_pos.pixel.x += 8;
                         break;
                    case DIRECTION_UP:
                         arrow_pos.pixel.y += 7;
                         break;
                    case DIRECTION_DOWN:
                         arrow_pos.pixel.y -= 11;
                         break;
                    }
                    arrow_pos.z += ARROW_SHOOT_HEIGHT;
                    arrow_array_spawn(&arrow_array, arrow_pos, player.face);
               }
               player.bow_draw_time = 0.0f;
          }

          if(!player_action.move_left && !player_action.move_right && !player_action.move_up && !player_action.move_down){
               player.walk_frame = 1;
          }else{
               player.walk_frame_time += dt;

               if(player.walk_frame_time > PLAYER_WALK_DELAY){
                    if(vec_magnitude(player.vel) > PLAYER_IDLE_SPEED){
                         player.walk_frame_time = 0.0f;

                         player.walk_frame += player.walk_frame_delta;
                         if(player.walk_frame > 2 || player.walk_frame < 0){
                              player.walk_frame = 1;
                              player.walk_frame_delta = -player.walk_frame_delta;
                         }
                    }else{
                         player.walk_frame = 1;
                         player.walk_frame_time = 0.0f;
                    }
               }
          }

          Vec_t pos_vec = pos_to_vec(player.pos);

#if 0
          // TODO: do we want this in the future?
          // figure out what room we should focus on
          Position_t room_center {};
          for(U32 i = 0; i < ELEM_COUNT(rooms); i++){
               if(coord_in_rect(vec_to_coord(pos_vec), rooms[i])){
                    S16 half_room_width = (rooms[i].right - rooms[i].left) / 2;
                    S16 half_room_height = (rooms[i].top - rooms[i].bottom) / 2;
                    Coord_t room_center_coord {(S16)(rooms[i].left + half_room_width),
                                               (S16)(rooms[i].bottom + half_room_height)};
                    room_center = coord_to_pos(room_center_coord);
                    break;
               }
          }
#else
          Position_t room_center = coord_to_pos(Coord_t{8, 8});
#endif

          Position_t camera_movement = room_center - camera;
          camera += camera_movement * 0.05f;

          float drag = 0.625f;

          // block movement
          for(S16 i = 0; i < block_array.count; i++){
               Block_t* block = block_array.elements + i;

               // TODO: compress with player movement
               Vec_t pos_delta = (block->accel * dt * dt * 0.5f) + (block->vel * dt);
               block->vel += block->accel * dt;
               block->vel *= drag;

               // TODO: blocks with velocity need to be checked against other blocks

               Position_t pre_move = block->pos;
               block->pos += pos_delta;

               bool stop_on_boundary_x = false;
               bool stop_on_boundary_y = false;
               bool held_up = false;

               Block_t* inside_block = nullptr;

               while((inside_block = block_inside_another_block(block_array.elements + i, block_quad_tree)) && blocks_at_collidable_height(block, inside_block)){
                    auto quadrant = relative_quadrant(block->pos.pixel, inside_block->pos.pixel);

                    switch(quadrant){
                    default:
                         break;
                    case DIRECTION_LEFT:
                         block->pos.pixel.x = inside_block->pos.pixel.x + TILE_SIZE_IN_PIXELS;
                         block->pos.decimal.x = 0.0f;
                         block->vel.x = 0.0f;
                         block->accel.x = 0.0f;
                         break;
                    case DIRECTION_RIGHT:
                         block->pos.pixel.x = inside_block->pos.pixel.x - TILE_SIZE_IN_PIXELS;
                         block->pos.decimal.x = 0.0f;
                         block->vel.x = 0.0f;
                         block->accel.x = 0.0f;
                         break;
                    case DIRECTION_DOWN:
                         block->pos.pixel.y = inside_block->pos.pixel.y + TILE_SIZE_IN_PIXELS;
                         block->pos.decimal.y = 0.0f;
                         block->vel.y = 0.0f;
                         block->accel.y = 0.0f;
                         break;
                    case DIRECTION_UP:
                         block->pos.pixel.y = inside_block->pos.pixel.y - TILE_SIZE_IN_PIXELS;
                         block->pos.decimal.y = 0.0f;
                         block->vel.y = 0.0f;
                         block->accel.y = 0.0f;
                         break;
                    }

                    if(block == last_block_pushed && quadrant == last_block_pushed_direction){
                         player.push_time = 0.0f;
                    }

                    if(block_on_ice(inside_block, &tilemap, interactive_quad_tree) && block_on_ice(block, &tilemap, interactive_quad_tree)){
                         block_push(inside_block, quadrant, &tilemap, interactive_quad_tree, block_quad_tree, false);
                    }
               }

               // get the current coord of the center of the block
               Pixel_t center = block->pos.pixel + Pixel_t{HALF_TILE_SIZE_IN_PIXELS, HALF_TILE_SIZE_IN_PIXELS};
               Coord_t coord = pixel_to_coord(center);

               // check for adjacent walls
               if(block->vel.x > 0.0f){
                    Coord_t check = coord + Coord_t{1, 0};
                    if(tilemap_is_solid(&tilemap, check)){
                         stop_on_boundary_x = true;
                    }else{
                         stop_on_boundary_x = quad_tree_interactive_solid_at(interactive_quad_tree, check);
                    }
               }else if(block->vel.x < 0.0f){
                    Coord_t check = coord + Coord_t{-1, 0};
                    if(tilemap_is_solid(&tilemap, check)){
                         stop_on_boundary_x = true;
                    }else{
                         stop_on_boundary_x = quad_tree_interactive_solid_at(interactive_quad_tree, check);
                    }
               }

               if(block->vel.y > 0.0f){
                    Coord_t check = coord + Coord_t{0, 1};
                    if(tilemap_is_solid(&tilemap, check)){
                         stop_on_boundary_y = true;
                    }else{
                         stop_on_boundary_y = quad_tree_interactive_solid_at(interactive_quad_tree, check);
                    }
               }else if(block->vel.y < 0.0f){
                    Coord_t check = coord + Coord_t{0, -1};
                    if(tilemap_is_solid(&tilemap, check)){
                         stop_on_boundary_y = true;
                    }else{
                         stop_on_boundary_y = quad_tree_interactive_solid_at(interactive_quad_tree, check);
                    }
               }

               if(block != last_block_pushed && !block_on_ice(block, &tilemap, interactive_quad_tree)){
                    stop_on_boundary_x = true;
                    stop_on_boundary_y = true;
               }

               if(stop_on_boundary_x){
                    // stop on tile boundaries separately for each axis
                    S16 boundary_x = range_passes_tile_boundary(pre_move.pixel.x, block->pos.pixel.x, block->push_start.x);
                    if(boundary_x){
                         block->pos.pixel.x = boundary_x;
                         block->pos.decimal.x = 0.0f;
                         block->vel.x = 0.0f;
                         block->accel.x = 0.0f;
                    }
               }

               if(stop_on_boundary_y){
                    S16 boundary_y = range_passes_tile_boundary(pre_move.pixel.y, block->pos.pixel.y, block->push_start.y);
                    if(boundary_y){
                         block->pos.pixel.y = boundary_y;
                         block->pos.decimal.y = 0.0f;
                         block->vel.y = 0.0f;
                         block->accel.y = 0.0f;
                    }
               }

               held_up = block_held_up_by_another_block(block, block_quad_tree);

               // TODO: should we care about the decimal component of the position ?
               Interactive_t* interactive = quad_tree_interactive_find_at(interactive_quad_tree, coord);
               if(interactive){
                    if(interactive->type == INTERACTIVE_TYPE_POPUP){
                         if(block->pos.z == interactive->popup.lift.ticks - 2){
                              block->pos.z++;
                              held_up = true;
                         }else if(block->pos.z > (interactive->popup.lift.ticks - 1)){
                              block->fall_time += dt;
                              if(block->fall_time >= FALL_TIME){
                                   block->fall_time -= FALL_TIME;
                                   block->pos.z--;
                              }
                              held_up = true;
                         }else if(block->pos.z == (interactive->popup.lift.ticks - 1)){
                              held_up = true;
                         }
                    }
               }

               if(!held_up && block->pos.z > 0){
                    block->fall_time += dt;
                    if(block->fall_time >= FALL_TIME){
                         block->fall_time -= FALL_TIME;
                         block->pos.z--;
                    }
               }
          }

          // illuminate and ice
          for(S16 i = 0; i < block_array.count; i++){
               Block_t* block = block_array.elements + i;
               if(block->element == ELEMENT_FIRE){
                    illuminate(block_get_coord(block), 255, &tilemap, block_quad_tree);
               }else if(block->element == ELEMENT_ICE){
                    spread_ice(block_get_coord(block), 1, &tilemap, interactive_quad_tree, block_quad_tree);
               }
          }

          // melt ice
          for(S16 i = 0; i < block_array.count; i++){
               Block_t* block = block_array.elements + i;
               if(block->element == ELEMENT_FIRE){
                    melt_ice(block_get_coord(block), 1, &tilemap, interactive_quad_tree, block_quad_tree);
               }
          }

          for(S16 i = 0; i < interactive_array.count; i++){
               Interactive_t* interactive = interactive_array.elements + i;
               if(interactive->type == INTERACTIVE_TYPE_LIGHT_DETECTOR){
                    Tile_t* tile = tilemap_get_tile(&tilemap, interactive->coord);

                    S16 px = interactive->coord.x * TILE_SIZE_IN_PIXELS;
                    S16 py = interactive->coord.y * TILE_SIZE_IN_PIXELS;
                    Rect_t coord_rect {(S16)(px - HALF_TILE_SIZE_IN_PIXELS),
                                       (S16)(py - HALF_TILE_SIZE_IN_PIXELS),
                                       (S16)(px + TILE_SIZE_IN_PIXELS + HALF_TILE_SIZE_IN_PIXELS),
                                       (S16)(py + TILE_SIZE_IN_PIXELS + HALF_TILE_SIZE_IN_PIXELS)};

                    S16 block_count = 0;
                    Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
                    quad_tree_find_in(block_quad_tree, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

                    Block_t* block = nullptr;
                    for(S16 b = 0; b < block_count; b++){
                         // blocks on the coordinate and on the ground block light
                         if(block_get_coord(blocks[b]) == interactive->coord && blocks[b]->pos.z == 0){
                              block = blocks[b];
                              break;
                         }
                    }

                    if(interactive->detector.on && (tile->light < LIGHT_DETECTOR_THRESHOLD || block)){
                         activate(&tilemap, interactive_quad_tree, interactive->coord);
                         interactive->detector.on = false;
                    }else if(!interactive->detector.on && tile->light >= LIGHT_DETECTOR_THRESHOLD && !block){
                         activate(&tilemap, interactive_quad_tree, interactive->coord);
                         interactive->detector.on = true;
                    }
               }else if(interactive->type == INTERACTIVE_TYPE_ICE_DETECTOR){
                    Tile_t* tile = tilemap_get_tile(&tilemap, interactive->coord);
                    if(tile){
                         if(interactive->detector.on && !tile_is_iced(tile)){
                              activate(&tilemap, interactive_quad_tree, interactive->coord);
                              interactive->detector.on = false;
                         }else if(!interactive->detector.on && tile_is_iced(tile)){
                              activate(&tilemap, interactive_quad_tree, interactive->coord);
                              interactive->detector.on = true;
                         }
                    }
               }
          }

          // player movement
          {
               user_movement = vec_normalize(user_movement);
               player.accel = user_movement * PLAYER_SPEED;

               Vec_t pos_delta = (player.accel * dt * dt * 0.5f) + (player.vel * dt);
               player.vel += player.accel * dt;
               player.vel *= drag;

               if(fabs(vec_magnitude(player.vel)) > PLAYER_SPEED){
                    player.vel = vec_normalize(player.vel) * PLAYER_SPEED;
               }

               // figure out tiles that are close by
               bool collide_with_tile = false;
               Coord_t player_coord = pos_to_coord(player.pos);
               Coord_t min = player_coord - Coord_t{1, 1};
               Coord_t max = player_coord + Coord_t{1, 1};
               // S8 player_top = player.pos.z + 2 * HEIGHT_INTERVAL;
               min = coord_clamp_zero_to_dim(min, tilemap.width - 1, tilemap.height - 1);
               max = coord_clamp_zero_to_dim(max, tilemap.width - 1, tilemap.height - 1);

               for(S16 y = min.y; y <= max.y; y++){
                    for(S16 x = min.x; x <= max.x; x++){
                         if(tilemap.tiles[y][x].id){
                              Coord_t coord {x, y};
                              player_collide_coord(player.pos, coord, player.radius, &pos_delta, &collide_with_tile);
                         }
                    }
               }

               for(S16 i = 0; i < block_array.count; i++){
                    bool collide_with_block = false;

                    Position_t relative = block_array.elements[i].pos - player.pos;
                    Vec_t bottom_left = pos_to_vec(relative);
                    if(vec_magnitude(bottom_left) > (2 * TILE_SIZE)) continue;

                    Vec_t top_left {bottom_left.x, bottom_left.y + TILE_SIZE};
                    Vec_t top_right {bottom_left.x + TILE_SIZE, bottom_left.y + TILE_SIZE};
                    Vec_t bottom_right {bottom_left.x + TILE_SIZE, bottom_left.y};

                    pos_delta += collide_circle_with_line(pos_delta, player.radius, bottom_left, top_left, &collide_with_block);
                    pos_delta += collide_circle_with_line(pos_delta, player.radius, top_left, top_right, &collide_with_block);
                    pos_delta += collide_circle_with_line(pos_delta, player.radius, bottom_left, bottom_right, &collide_with_block);
                    pos_delta += collide_circle_with_line(pos_delta, player.radius, bottom_right, top_right, &collide_with_block);

                    if(collide_with_block){
                         auto player_quadrant = relative_quadrant(player.pos.pixel, block_array.elements[i].pos.pixel +
                                                                  Pixel_t{HALF_TILE_SIZE_IN_PIXELS, HALF_TILE_SIZE_IN_PIXELS});
                         if(player_quadrant == player.face &&
                            (user_movement.x != 0.0f || user_movement.y != 0.0f)){ // also check that the player is actually pushing against the block
                              if(block_to_push == nullptr){
                                   block_to_push = block_array.elements + i;
                                   last_block_pushed_direction = player.face;
                              }
                         }
                    }
               }

               for(S16 y = min.y; y <= max.y; y++){
                    for(S16 x = min.x; x <= max.x; x++){
                         Coord_t coord {x, y};
                         if(quad_tree_interactive_solid_at(interactive_quad_tree, coord)){
                              player_collide_coord(player.pos, coord, player.radius, &pos_delta, &collide_with_tile);
                         }
                    }
               }

               if(block_to_push){
                    F32 before_time = player.push_time;

                    player.push_time += dt;
                    if(player.push_time > BLOCK_PUSH_TIME){
                         if(before_time <= BLOCK_PUSH_TIME) undo_commit(&undo, &player, &tilemap, &block_array, &interactive_array);
                         block_push(block_to_push, player.face, &tilemap, interactive_quad_tree, block_quad_tree, true);
                         if(block_to_push->pos.z > 0) player.push_time = -0.5f;
                    }
               }else{
                    player.push_time = 0;
               }

               player.pos += pos_delta;
          }

          if(suite && !show_suite) continue;

          glClear(GL_COLOR_BUFFER_BIT);

          Position_t screen_camera = camera - Vec_t{0.5f, 0.5f} + Vec_t{HALF_TILE_SIZE, HALF_TILE_SIZE};

          // tilemap
          Coord_t min = pos_to_coord(screen_camera);
          Coord_t max = min + Coord_t{ROOM_TILE_SIZE, ROOM_TILE_SIZE};
          min = coord_clamp_zero_to_dim(min, tilemap.width - 1, tilemap.height - 1);
          max = coord_clamp_zero_to_dim(max, tilemap.width - 1, tilemap.height - 1);
          Position_t tile_bottom_left = coord_to_pos(min);
          Vec_t camera_offset = pos_to_vec(tile_bottom_left - screen_camera);

          glBindTexture(GL_TEXTURE_2D, theme_texture);
          glBegin(GL_QUADS);
          glColor3f(1.0f, 1.0f, 1.0f);
          for(S16 y = max.y; y >= min.y; y--){
               for(S16 x = min.x; x <= max.x; x++){
                    // draw tile
                    Tile_t* tile = tilemap.tiles[y] + x;

                    Vec_t tile_pos {(F32)(x - min.x) * TILE_SIZE + camera_offset.x,
                                    (F32)(y - min.y) * TILE_SIZE + camera_offset.y};

                    tile_id_draw(tile->id, tile_pos);
                    tile_flags_draw(tile->flags, tile_pos);

                    // draw flat interactives that could be covered by ice
                    Interactive_t* interactive = quad_tree_find_at(interactive_quad_tree, x, y);
                    if(interactive){
                         if(interactive->type == INTERACTIVE_TYPE_PRESSURE_PLATE){
                              if(!tile_is_iced(tile) && interactive->pressure_plate.iced_under){
                                   // TODO: compress with above ice drawing code
                                   glEnd();

                                   // get state ready for ice
                                   glBindTexture(GL_TEXTURE_2D, 0);
                                   glColor4f(196.0f / 255.0f, 217.0f / 255.0f, 1.0f, 0.45f);
                                   glBegin(GL_QUADS);
                                   glVertex2f(tile_pos.x, tile_pos.y);
                                   glVertex2f(tile_pos.x, tile_pos.y + TILE_SIZE);
                                   glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y + TILE_SIZE);
                                   glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y);
                                   glEnd();

                                   // reset state back to default
                                   glBindTexture(GL_TEXTURE_2D, theme_texture);
                                   glBegin(GL_QUADS);
                                   glColor3f(1.0f, 1.0f, 1.0f);
                              }

                              interactive_draw(interactive, pos_to_vec(coord_to_pos(interactive->coord) - screen_camera));
                         }else if(interactive->type == INTERACTIVE_TYPE_POPUP && interactive->popup.lift.ticks == 1){
                              if(interactive->popup.iced){
                                   tile_pos.y += interactive->popup.lift.ticks * PIXEL_SIZE;
                                   Vec_t tex_vec = theme_frame(3, 12);
                                   glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
                                   glTexCoord2f(tex_vec.x, tex_vec.y);
                                   glVertex2f(tile_pos.x, tile_pos.y);
                                   glTexCoord2f(tex_vec.x, tex_vec.y + THEME_FRAME_HEIGHT);
                                   glVertex2f(tile_pos.x, tile_pos.y + TILE_SIZE);
                                   glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y + THEME_FRAME_HEIGHT);
                                   glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y + TILE_SIZE);
                                   glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y);
                                   glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y);
                                   glColor3f(1.0f, 1.0f, 1.0f);
                              }

                              interactive_draw(interactive, pos_to_vec(coord_to_pos(interactive->coord) - screen_camera));
                         }else if(interactive->type == INTERACTIVE_TYPE_LIGHT_DETECTOR ||
                                  interactive->type == INTERACTIVE_TYPE_ICE_DETECTOR){
                              interactive_draw(interactive, pos_to_vec(coord_to_pos(interactive->coord) - screen_camera));
                         }
                    }

                    if(tile_is_iced(tile)){
                         glEnd();

                         // get state ready for ice
                         glBindTexture(GL_TEXTURE_2D, 0);
                         glColor4f(196.0f / 255.0f, 217.0f / 255.0f, 1.0f, 0.45f);
                         glBegin(GL_QUADS);
                         glVertex2f(tile_pos.x, tile_pos.y);
                         glVertex2f(tile_pos.x, tile_pos.y + TILE_SIZE);
                         glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y + TILE_SIZE);
                         glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y);
                         glEnd();

                         // reset state back to default
                         glBindTexture(GL_TEXTURE_2D, theme_texture);
                         glBegin(GL_QUADS);
                         glColor3f(1.0f, 1.0f, 1.0f);
                    }
               }
          }

          for(S16 y = max.y; y >= min.y; y--){
               for(S16 x = min.x; x <= max.x; x++){
                    Coord_t coord {x, y};

                    // draw interactive
                    Interactive_t* interactive = quad_tree_find_at(interactive_quad_tree, coord.x, coord.y);
                    if(interactive){
                         if(interactive->type == INTERACTIVE_TYPE_PRESSURE_PLATE ||
                            interactive->type == INTERACTIVE_TYPE_ICE_DETECTOR ||
                            interactive->type == INTERACTIVE_TYPE_LIGHT_DETECTOR ||
                            (interactive->type == INTERACTIVE_TYPE_POPUP && interactive->popup.lift.ticks == 1)){
                              // pass, these are flat
                         }else{
                              interactive_draw(interactive, pos_to_vec(coord_to_pos(interactive->coord) - screen_camera));

                              if(interactive->type == INTERACTIVE_TYPE_POPUP && interactive->popup.iced){
                                   Vec_t tile_pos {(F32)(x - min.x) * TILE_SIZE + camera_offset.x,
                                                   (F32)(y - min.y) * TILE_SIZE + camera_offset.y};
                                   tile_pos.y += interactive->popup.lift.ticks * PIXEL_SIZE;
                                   Vec_t tex_vec = theme_frame(3, 12);
                                   glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
                                   glTexCoord2f(tex_vec.x, tex_vec.y);
                                   glVertex2f(tile_pos.x, tile_pos.y);
                                   glTexCoord2f(tex_vec.x, tex_vec.y + THEME_FRAME_HEIGHT);
                                   glVertex2f(tile_pos.x, tile_pos.y + TILE_SIZE);
                                   glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y + THEME_FRAME_HEIGHT);
                                   glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y + TILE_SIZE);
                                   glTexCoord2f(tex_vec.x + THEME_FRAME_WIDTH, tex_vec.y);
                                   glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y);
                                   glColor3f(1.0f, 1.0f, 1.0f);
                              }
                         }
                    }

                    // draw block
                    S16 px = coord.x * TILE_SIZE_IN_PIXELS;
                    S16 py = coord.y * TILE_SIZE_IN_PIXELS;
                    Rect_t coord_rect {(S16)(px - 1), (S16)(py - 1), (S16)(px * TILE_SIZE_IN_PIXELS + 1), (S16)(py * TILE_SIZE_IN_PIXELS + 1)};

                    S16 block_count = 0;
                    Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
                    quad_tree_find_in(block_quad_tree, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

                    for(S16 i = 0; i < block_count; i++){
                         Block_t* block = blocks[i];
                         if(block_get_coord(block) == coord){
                              Position_t block_camera_offset = block->pos - screen_camera;
                              block_camera_offset.pixel.y += block->pos.z;
                              block_draw(block, pos_to_vec(block_camera_offset));
                         }
                    }

                    // player
                    if(pos_to_coord(player.pos) == coord){
                         S8 player_frame_y = player.face;
                         if(player.push_time > FLT_EPSILON) player_frame_y += 4;
                         if(player.has_bow){
                              player_frame_y += 8;
                              if(player.bow_draw_time > (PLAYER_BOW_DRAW_DELAY / 2.0f)){
                                   player_frame_y += 8;
                                   if(player.bow_draw_time >= PLAYER_BOW_DRAW_DELAY){
                                        player_frame_y += 4;
                                   }
                              }
                         }
                         Vec_t tex_vec = player_frame(player.walk_frame, player_frame_y);
                         pos_vec.y += (5.0f * PIXEL_SIZE);

                         glEnd();
                         glBindTexture(GL_TEXTURE_2D, player_texture);
                         glBegin(GL_QUADS);
                         glColor3f(1.0f, 1.0f, 1.0f);
                         glTexCoord2f(tex_vec.x, tex_vec.y);
                         glVertex2f(pos_vec.x - HALF_TILE_SIZE, pos_vec.y - HALF_TILE_SIZE);
                         glTexCoord2f(tex_vec.x, tex_vec.y + PLAYER_FRAME_HEIGHT);
                         glVertex2f(pos_vec.x - HALF_TILE_SIZE, pos_vec.y + HALF_TILE_SIZE);
                         glTexCoord2f(tex_vec.x + PLAYER_FRAME_WIDTH, tex_vec.y + PLAYER_FRAME_HEIGHT);
                         glVertex2f(pos_vec.x + HALF_TILE_SIZE, pos_vec.y + HALF_TILE_SIZE);
                         glTexCoord2f(tex_vec.x + PLAYER_FRAME_WIDTH, tex_vec.y);
                         glVertex2f(pos_vec.x + HALF_TILE_SIZE, pos_vec.y - HALF_TILE_SIZE);
                         glEnd();

                         glBindTexture(GL_TEXTURE_2D, theme_texture);
                         glBegin(GL_QUADS);
                         glColor3f(1.0f, 1.0f, 1.0f);
                    }

                    // draw arrows
                    static Vec_t arrow_tip_offset[DIRECTION_COUNT] = {
                         {0.0f,               9.0f * PIXEL_SIZE},
                         {8.0f * PIXEL_SIZE,  16.0f * PIXEL_SIZE},
                         {16.0f * PIXEL_SIZE, 9.0f * PIXEL_SIZE},
                         {8.0f * PIXEL_SIZE,  0.0f * PIXEL_SIZE},
                    };

                    for(S16 a = 0; a < ARROW_ARRAY_MAX; a++){
                         Arrow_t* arrow = arrow_array.arrows + a;
                         if(!arrow->alive) continue;
                         Vec_t arrow_vec = pos_to_vec(arrow->pos - screen_camera);
                         arrow_vec.x -= arrow_tip_offset[arrow->face].x;
                         arrow_vec.y -= arrow_tip_offset[arrow->face].y;

                         glEnd();
                         glBindTexture(GL_TEXTURE_2D, arrow_texture);
                         glBegin(GL_QUADS);
                         glColor3f(1.0f, 1.0f, 1.0f);

                         // shadow
                         //arrow_vec.y -= (arrow->pos.z * PIXEL_SIZE);
                         Vec_t tex_vec = arrow_frame(arrow->face, 1);
                         glTexCoord2f(tex_vec.x, tex_vec.y);
                         glVertex2f(arrow_vec.x, arrow_vec.y);
                         glTexCoord2f(tex_vec.x, tex_vec.y + ARROW_FRAME_HEIGHT);
                         glVertex2f(arrow_vec.x, arrow_vec.y + TILE_SIZE);
                         glTexCoord2f(tex_vec.x + ARROW_FRAME_WIDTH, tex_vec.y + ARROW_FRAME_HEIGHT);
                         glVertex2f(arrow_vec.x + TILE_SIZE, arrow_vec.y + TILE_SIZE);
                         glTexCoord2f(tex_vec.x + ARROW_FRAME_WIDTH, tex_vec.y);
                         glVertex2f(arrow_vec.x + TILE_SIZE, arrow_vec.y);

                         arrow_vec.y += (arrow->pos.z * PIXEL_SIZE);

                         S8 y_frame = 0;
                         if(arrow->element) y_frame = 2 + ((arrow->element - 1) * 4);

                         tex_vec = arrow_frame(arrow->face, y_frame);
                         glTexCoord2f(tex_vec.x, tex_vec.y);
                         glVertex2f(arrow_vec.x, arrow_vec.y);
                         glTexCoord2f(tex_vec.x, tex_vec.y + ARROW_FRAME_HEIGHT);
                         glVertex2f(arrow_vec.x, arrow_vec.y + TILE_SIZE);
                         glTexCoord2f(tex_vec.x + ARROW_FRAME_WIDTH, tex_vec.y + ARROW_FRAME_HEIGHT);
                         glVertex2f(arrow_vec.x + TILE_SIZE, arrow_vec.y + TILE_SIZE);
                         glTexCoord2f(tex_vec.x + ARROW_FRAME_WIDTH, tex_vec.y);
                         glVertex2f(arrow_vec.x + TILE_SIZE, arrow_vec.y);

                         glEnd();

                         glBindTexture(GL_TEXTURE_2D, theme_texture);
                         glBegin(GL_QUADS);
                         glColor3f(1.0f, 1.0f, 1.0f);
                    }
               }
          }
          glEnd();

          // player circle
          Position_t player_camera_offset = player.pos - screen_camera;
          pos_vec = pos_to_vec(player_camera_offset);

          glBindTexture(GL_TEXTURE_2D, 0);
          glBegin(GL_LINES);
          if(block_to_push){
               glColor3f(1.0f, 0.0f, 0.0f);
          }else{
               glColor3f(1.0f, 1.0f, 1.0f);
          }
          Vec_t prev_vec {pos_vec.x + player.radius, pos_vec.y};
          S32 segments = 32;
          F32 delta = 3.14159f * 2.0f / (F32)(segments);
          F32 angle = 0.0f  + delta;
          for(S32 i = 0; i <= segments; i++){
               F32 dx = cos(angle) * player.radius;
               F32 dy = sin(angle) * player.radius;

               glVertex2f(prev_vec.x, prev_vec.y);
               glVertex2f(pos_vec.x + dx, pos_vec.y + dy);
               prev_vec.x = pos_vec.x + dx;
               prev_vec.y = pos_vec.y + dy;
               angle += delta;
          }
          glEnd();

#if 0
          // light
          glBindTexture(GL_TEXTURE_2D, 0);
          glBegin(GL_QUADS);
          for(S16 y = min.y; y <= max.y; y++){
               for(S16 x = min.x; x <= max.x; x++){
                    Tile_t* tile = tilemap.tiles[y] + x;

                    Vec_t tile_pos {(F32)(x - min.x) * TILE_SIZE + camera_offset.x,
                                    (F32)(y - min.y) * TILE_SIZE + camera_offset.y};
                    glColor4f(0.0f, 0.0f, 0.0f, (F32)(255 - tile->light) / 255.0f);
                    glVertex2f(tile_pos.x, tile_pos.y);
                    glVertex2f(tile_pos.x, tile_pos.y + TILE_SIZE);
                    glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y + TILE_SIZE);
                    glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y);

               }
          }
          glEnd();
#endif

          // player start
          selection_draw(player_start, player_start, screen_camera, 0.0f, 1.0f, 0.0f);

          // editor
          switch(editor.mode){
          default:
               break;
          case EDITOR_MODE_OFF:
               // pass
               break;
          case EDITOR_MODE_CATEGORY_SELECT:
          {
               glBindTexture(GL_TEXTURE_2D, theme_texture);
               glBegin(GL_QUADS);
               glColor3f(1.0f, 1.0f, 1.0f);

               Vec_t vec = {0.0f, 0.0f};

               for(S32 g = 0; g < editor.category_array.count; ++g){
                    auto* category = editor.category_array.elements + g;
                    auto* stamp_array = category->elements + 0;

                    for(S16 s = 0; s < stamp_array->count; s++){
                         auto* stamp = stamp_array->elements + s;
                         if(g && (g % ROOM_TILE_SIZE) == 0){
                              vec.x = 0.0f;
                              vec.y += TILE_SIZE;
                         }

                         switch(stamp->type){
                         default:
                              break;
                         case STAMP_TYPE_TILE_ID:
                              tile_id_draw(stamp->tile_id, vec);
                              break;
                         case STAMP_TYPE_TILE_FLAGS:
                              tile_flags_draw(stamp->tile_flags, vec);
                              break;
                         case STAMP_TYPE_BLOCK:
                         {
                              Block_t block = {};
                              block.element = stamp->block.element;
                              block.face = stamp->block.face;

                              block_draw(&block, vec);
                         } break;
                         case STAMP_TYPE_INTERACTIVE:
                         {
                              interactive_draw(&stamp->interactive, vec);
                         } break;
                         }
                    }

                    vec.x += TILE_SIZE;
               }

               glEnd();
          } break;
          case EDITOR_MODE_STAMP_SELECT:
          case EDITOR_MODE_STAMP_HIDE:
          {
               glBindTexture(GL_TEXTURE_2D, theme_texture);
               glBegin(GL_QUADS);
               glColor3f(1.0f, 1.0f, 1.0f);

               // draw stamp at mouse
               auto* stamp_array = editor.category_array.elements[editor.category].elements + editor.stamp;
               Coord_t mouse_coord = mouse_select_coord(mouse_screen);

               for(S16 s = 0; s < stamp_array->count; s++){
                    auto* stamp = stamp_array->elements + s;
                    Vec_t stamp_pos = coord_to_screen_position(mouse_coord + stamp->offset);
                    switch(stamp->type){
                    default:
                         break;
                    case STAMP_TYPE_TILE_ID:
                         tile_id_draw(stamp->tile_id, stamp_pos);
                         break;
                    case STAMP_TYPE_TILE_FLAGS:
                         tile_flags_draw(stamp->tile_flags, stamp_pos);
                         break;
                    case STAMP_TYPE_BLOCK:
                    {
                         Block_t block = {};
                         block.element = stamp->block.element;
                         block.face = stamp->block.face;
                         block_draw(&block, stamp_pos);
                    } break;
                    case STAMP_TYPE_INTERACTIVE:
                    {
                         interactive_draw(&stamp->interactive, stamp_pos);
                    } break;
                    }
               }

               if(editor.mode == EDITOR_MODE_STAMP_SELECT){
                    // draw stamps to select from at the bottom
                    Vec_t pos = {0.0f, 0.0f};
                    S16 row_height = 1;
                    auto* category = editor.category_array.elements + editor.category;

                    for(S32 g = 0; g < category->count; ++g){
                         stamp_array = category->elements + g;
                         Coord_t dimensions = stamp_array_dimensions(stamp_array);
                         if(dimensions.y > row_height) row_height = dimensions.y;

                         for(S32 s = 0; s < stamp_array->count; s++){
                              auto* stamp = stamp_array->elements + s;
                              Vec_t stamp_vec = pos + coord_to_vec(stamp->offset);


                              switch(stamp->type){
                              default:
                                   break;
                              case STAMP_TYPE_TILE_ID:
                                   tile_id_draw(stamp->tile_id, stamp_vec);
                                   break;
                              case STAMP_TYPE_TILE_FLAGS:
                                   tile_flags_draw(stamp->tile_flags, stamp_vec);
                                   break;
                              case STAMP_TYPE_BLOCK:
                              {
                                   Block_t block = {};
                                   block.element = stamp->block.element;
                                   block.face = stamp->block.face;
                                   block_draw(&block, stamp_vec);
                              } break;
                              case STAMP_TYPE_INTERACTIVE:
                              {
                                   interactive_draw(&stamp->interactive, stamp_vec);
                              } break;
                              }
                         }

                         pos.x += (dimensions.x * TILE_SIZE);
                         if(pos.x >= 1.0f){
                              pos.x = 0.0f;
                              pos.y += row_height * TILE_SIZE;
                              row_height = 1;
                         }
                    }
               }

               glEnd();
          } break;
          case EDITOR_MODE_CREATE_SELECTION:
               selection_draw(editor.selection_start, editor.selection_end, screen_camera, 1.0f, 0.0f, 0.0f);
               break;
          case EDITOR_MODE_SELECTION_MANIPULATION:
          {
               glBindTexture(GL_TEXTURE_2D, theme_texture);
               glBegin(GL_QUADS);
               glColor3f(1.0f, 1.0f, 1.0f);

               for(S32 g = 0; g < editor.selection.count; ++g){
                    auto* stamp = editor.selection.elements + g;
                    Position_t stamp_pos = coord_to_pos(editor.selection_start + stamp->offset);
                    Vec_t stamp_vec = pos_to_vec(stamp_pos);

                    switch(stamp->type){
                    default:
                         break;
                    case STAMP_TYPE_TILE_ID:
                         tile_id_draw(stamp->tile_id, stamp_vec);
                         break;
                    case STAMP_TYPE_TILE_FLAGS:
                         tile_flags_draw(stamp->tile_flags, stamp_vec);
                         break;
                    case STAMP_TYPE_BLOCK:
                    {
                         Block_t block = {};
                         block.element = stamp->block.element;
                         block.face = stamp->block.face;
                         block_draw(&block, stamp_vec);
                    } break;
                    case STAMP_TYPE_INTERACTIVE:
                    {
                         interactive_draw(&stamp->interactive, stamp_vec);
                    } break;
                    }
               }
               glEnd();

               Rect_t selection_bounds = editor_selection_bounds(&editor);
               Coord_t min_coord {selection_bounds.left, selection_bounds.bottom};
               Coord_t max_coord {selection_bounds.right, selection_bounds.top};
               selection_draw(min_coord, max_coord, screen_camera, 1.0f, 0.0f, 0.0f);
          } break;
          }

          SDL_GL_SwapWindow(window);
     }

     switch(demo_mode){
     default:
          break;
     case DEMO_MODE_RECORD:
          player_action_perform(&player_action, &player, PLAYER_ACTION_TYPE_END_DEMO, demo_mode,
                                demo_file, frame_count);
          // save map and player position
          save_map_to_file(demo_file, player_start, &tilemap, &block_array, &interactive_array);
          fwrite(&player.pos.pixel, sizeof(player.pos.pixel), 1, demo_file);
          fclose(demo_file);
          break;
     case DEMO_MODE_PLAY:
          fclose(demo_file);
          break;
     }

     quad_tree_free(interactive_quad_tree);

     destroy(&undo);
     destroy(&tilemap);

     if(!suite){
          glDeleteTextures(1, &theme_texture);
          glDeleteTextures(1, &player_texture);
          glDeleteTextures(1, &arrow_texture);

          SDL_GL_DeleteContext(opengl_context);
          SDL_DestroyWindow(window);
          SDL_Quit();
     }

     Log_t::destroy();

     return 0;
}
