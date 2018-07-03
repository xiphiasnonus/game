#include "world.h"
#include "defines.h"
#include "conversion.h"
#include "map_format.h"
#include "portal_exit.h"
#include "utils.h"
#include "collision.h"

// linux
#include <dirent.h>
#include <string.h>
#include <math.h>

bool load_map_number(S32 map_number, Coord_t* player_start, World_t* world){
     // search through directory to find file starting with 3 digit map number
     DIR* d = opendir("content");
     if(!d) return false;
     struct dirent* dir;
     char filepath[512] = {};
     char match[4] = {};
     snprintf(match, 4, "%03d", map_number);
     while((dir = readdir(d)) != nullptr){
          if(strncmp(dir->d_name, match, 3) == 0 &&
             strstr(dir->d_name, ".bm")){ // TODO: create strendswith() func for this?
               snprintf(filepath, 512, "content/%s", dir->d_name);
               break;
          }
     }

     closedir(d);

     if(!filepath[0]) return false;

     LOG("load map %s\n", filepath);
     return load_map(filepath, player_start, &world->tilemap, &world->blocks, &world->interactives);
}

void setup_map(Coord_t player_start, World_t* world, Undo_t* undo){
     world->player = {};
     world->player.walk_frame_delta = 1;
     world->player.pos = coord_to_pos_at_tile_center(player_start);
     world->player.has_bow = true;

     init(&world->arrows);

     quad_tree_free(world->interactive_qt);
     world->interactive_qt = quad_tree_build(&world->interactives);

     quad_tree_free(world->block_qt);
     world->block_qt = quad_tree_build(&world->blocks);

     destroy(undo);
     init(undo, UNDO_MEMORY, world->tilemap.width, world->tilemap.height, world->blocks.count, world->interactives.count);
     undo_snapshot(undo, &world->player, &world->tilemap, &world->blocks, &world->interactives);
}

static void toggle_electricity(TileMap_t* tilemap, QuadTreeNode_t<Interactive_t>* interactive_quad_tree, Coord_t coord,
                               Direction_t direction, bool from_wire, bool activated_by_door){
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
               // open connecting door
               if(!activated_by_door) toggle_electricity(tilemap, interactive_quad_tree,
                                                         coord_move(coord, interactive->door.face, 3),
                                                         interactive->door.face, from_wire, true);
               break;
          case INTERACTIVE_TYPE_PORTAL:
               if(from_wire) interactive->portal.on = !interactive->portal.on;
               break;
          }
     }

     if((tile->flags & (TILE_FLAG_WIRE_LEFT | TILE_FLAG_WIRE_UP | TILE_FLAG_WIRE_RIGHT | TILE_FLAG_WIRE_DOWN)) ||
        (interactive && interactive->type == INTERACTIVE_TYPE_WIRE_CROSS &&
         interactive->wire_cross.mask & (TILE_FLAG_WIRE_LEFT | TILE_FLAG_WIRE_UP | TILE_FLAG_WIRE_RIGHT | TILE_FLAG_WIRE_DOWN))){
          bool wire_cross = false;

          if(interactive && interactive->type == INTERACTIVE_TYPE_WIRE_CROSS){
               switch(direction){
               default:
                    return;
               case DIRECTION_LEFT:
                    if(tile->flags & TILE_FLAG_WIRE_RIGHT){
                         TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_STATE);
                    }else if(interactive->wire_cross.mask & DIRECTION_MASK_RIGHT){
                         interactive->wire_cross.on = !interactive->wire_cross.on;
                         wire_cross = true;
                    }else{
                         return;
                    }
                    break;
               case DIRECTION_RIGHT:
                    if(tile->flags & TILE_FLAG_WIRE_LEFT){
                         TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_STATE);
                    }else if(interactive->wire_cross.mask & DIRECTION_MASK_LEFT){
                         interactive->wire_cross.on = !interactive->wire_cross.on;
                         wire_cross = true;
                    }else{
                         return;
                    }
                    break;
               case DIRECTION_UP:
                    if(tile->flags & TILE_FLAG_WIRE_DOWN){
                         TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_STATE);
                    }else if(interactive->wire_cross.mask & DIRECTION_MASK_DOWN){
                         interactive->wire_cross.on = !interactive->wire_cross.on;
                         wire_cross = true;
                    }else{
                         return;
                    }
                    break;
               case DIRECTION_DOWN:
                    if(tile->flags & TILE_FLAG_WIRE_UP){
                         TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_STATE);
                    }else if(interactive->wire_cross.mask & DIRECTION_MASK_UP){
                         interactive->wire_cross.on = !interactive->wire_cross.on;
                         wire_cross = true;
                    }else{
                         return;
                    }
                    break;
               }
          }else{
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
               TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_STATE);
          }

          if(wire_cross){
               if(interactive->wire_cross.mask & DIRECTION_MASK_LEFT && direction != DIRECTION_RIGHT){
                    toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_LEFT, true, false);
               }

               if(interactive->wire_cross.mask & DIRECTION_MASK_RIGHT && direction != DIRECTION_LEFT){
                    toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_RIGHT, true, false);
               }

               if(interactive->wire_cross.mask & DIRECTION_MASK_DOWN && direction != DIRECTION_UP){
                    toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_DOWN, true, false);
               }

               if(interactive->wire_cross.mask & DIRECTION_MASK_UP && direction != DIRECTION_DOWN){
                    toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_UP, true, false);
               }
          }else{
               if(tile->flags & TILE_FLAG_WIRE_LEFT && direction != DIRECTION_RIGHT){
                    toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_LEFT, true, false);
               }

               if(tile->flags & TILE_FLAG_WIRE_RIGHT && direction != DIRECTION_LEFT){
                    toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_RIGHT, true, false);
               }

               if(tile->flags & TILE_FLAG_WIRE_DOWN && direction != DIRECTION_UP){
                    toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_DOWN, true, false);
               }

               if(tile->flags & TILE_FLAG_WIRE_UP && direction != DIRECTION_DOWN){
                    toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, DIRECTION_UP, true, false);
               }
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
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_MID) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_MID_ON);
                    break;
               case DIRECTION_UP:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_LEFT) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_LEFT_ON);
                    break;
               case DIRECTION_DOWN:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_RIGHT) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_RIGHT_ON);
                    break;
               }
               break;
          case DIRECTION_RIGHT:
               switch(direction){
               default:
                    break;
               case DIRECTION_RIGHT:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_MID) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_MID_ON);
                    break;
               case DIRECTION_DOWN:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_LEFT) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_LEFT_ON);
                    break;
               case DIRECTION_UP:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_RIGHT) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_RIGHT_ON);
                    break;
               }
               break;
          case DIRECTION_DOWN:
               switch(direction){
               default:
                    break;
               case DIRECTION_DOWN:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_MID) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_MID_ON);
                    break;
               case DIRECTION_LEFT:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_LEFT) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_LEFT_ON);
                    break;
               case DIRECTION_RIGHT:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_RIGHT) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_RIGHT_ON);
                    break;
               }
               break;
          case DIRECTION_UP:
               switch(direction){
               default:
                    break;
               case DIRECTION_UP:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_MID) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_MID_ON);
                    break;
               case DIRECTION_RIGHT:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_LEFT) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_LEFT_ON);
                    break;
               case DIRECTION_LEFT:
                    if(tile->flags & TILE_FLAG_WIRE_CLUSTER_RIGHT) TOGGLE_BIT_FLAG(tile->flags, TILE_FLAG_WIRE_CLUSTER_RIGHT_ON);
                    break;
               }
               break;
          }

          bool all_on_after = tile_flags_cluster_all_on(tile->flags);

          if(all_on_before != all_on_after){
               toggle_electricity(tilemap, interactive_quad_tree, adjacent_coord, cluster_direction, true, false);
          }
     }
}

void activate(World_t* world, Coord_t coord){
     Interactive_t* interactive = quad_tree_interactive_find_at(world->interactive_qt, coord);
     if(!interactive) return;

     if(interactive->type != INTERACTIVE_TYPE_LEVER &&
        interactive->type != INTERACTIVE_TYPE_PRESSURE_PLATE &&
        interactive->type != INTERACTIVE_TYPE_LIGHT_DETECTOR &&
        interactive->type != INTERACTIVE_TYPE_ICE_DETECTOR) return;

     toggle_electricity(&world->tilemap, world->interactive_qt, coord, DIRECTION_LEFT, false, false);
     toggle_electricity(&world->tilemap, world->interactive_qt, coord, DIRECTION_RIGHT, false, false);
     toggle_electricity(&world->tilemap, world->interactive_qt, coord, DIRECTION_UP, false, false);
     toggle_electricity(&world->tilemap, world->interactive_qt, coord, DIRECTION_DOWN, false, false);
}

Vec_t move_player_position_through_world(Position_t position, Vec_t pos_delta, Direction_t player_face, Coord_t* skip_coord,
                                         Player_t* player, World_t* world, Block_t** block_to_push, Direction_t* last_block_pushed_direction,
                                         bool* collided_with_interactive, bool* resetting){
     // figure out tiles that are close by
     Position_t final_player_pos = position + pos_delta;
     Coord_t player_coord = pos_to_coord(final_player_pos);

     // NOTE: this assumes the player can't move more than 1 coord in 1 frame
     Coord_t min = player_coord - Coord_t{1, 1};
     Coord_t max = player_coord + Coord_t{1, 1};
     // S8 player_top = player.pos.z + 2 * HEIGHT_INTERVAL;
     min = coord_clamp_zero_to_dim(min, world->tilemap.width - 1, world->tilemap.height - 1);
     max = coord_clamp_zero_to_dim(max, world->tilemap.width - 1, world->tilemap.height - 1);

     // TODO: convert to use quad tree
     Vec_t collided_block_delta {};

     // where the player has collided with blocks
     DirectionMask_t collided_blocks_mask_dir = DIRECTION_MASK_NONE;
     Block_t* collided_blocks[DIRECTION_COUNT] = {};

     for(S16 i = 0; i < world->blocks.count; i++){
          Vec_t pos_delta_save = pos_delta;

          bool collide_with_block = false;
          Position_t block_pos = world->blocks.elements[i].pos;
          position_collide_with_rect(position, block_pos, &pos_delta, &collide_with_block);
          Coord_t block_coord = block_get_coord(world->blocks.elements + i);
          U8 portal_rotations = 0;

          if(!collide_with_block){
               // check if the block is in a portal and try to collide with it
               Vec_t coord_offset = pos_to_vec(world->blocks.elements[i].pos +
                                               pixel_to_pos(HALF_TILE_SIZE_PIXEL) -
                                               coord_to_pos_at_tile_center(block_coord));
               for(S8 r = 0; r < DIRECTION_COUNT && !collide_with_block; r++){
                    Coord_t check_coord = block_coord + (Direction_t)(r);
                    Interactive_t* interactive = quad_tree_interactive_find_at(world->interactive_qt, check_coord);

                    if(is_active_portal(interactive)){
                         PortalExit_t portal_exits = find_portal_exits(check_coord, &world->tilemap, world->interactive_qt);

                         for(S8 d = 0; d < DIRECTION_COUNT && !collide_with_block; d++){
                              Vec_t final_coord_offset = rotate_vec_between_dirs_clockwise(interactive->portal.face, (Direction_t)(d), coord_offset);

                              for(S8 p = 0; p < portal_exits.directions[d].count; p++){
                                   if(portal_exits.directions[d].coords[p] == check_coord) continue;

                                   Position_t portal_pos = coord_to_pos_at_tile_center(portal_exits.directions[d].coords[p]) + final_coord_offset -
                                                           pixel_to_pos(HALF_TILE_SIZE_PIXEL);
                                   position_collide_with_rect(position, portal_pos, &pos_delta, &collide_with_block);

                                   if(collide_with_block){
                                        block_pos = portal_pos;
                                        if(is_active_portal(interactive)){
                                             player_face = direction_opposite(interactive->portal.face);
                                             portal_rotations = portal_rotations_between(interactive->portal.face, (Direction_t)(d));
                                             break;
                                        }
                                   }
                              }
                         }
                    }
               }
          }

          if(collide_with_block){
               Vec_t pos_delta_diff = pos_delta - pos_delta_save;
               collided_block_delta = vec_rotate_quadrants_clockwise(pos_delta_diff, 4 - portal_rotations);
               auto collided_block_dir = relative_quadrant(position.pixel, block_pos.pixel + HALF_TILE_SIZE_PIXEL);
               auto collided_block = world->blocks.elements + i;
               Position_t pre_move = collided_block->pos;

#ifndef BLOCKS_SQUISH_PLAYER
               // this stops the block when it moves into the player
               Vec_t rotated_accel = vec_rotate_quadrants_clockwise(collided_block->accel, portal_rotations);
               Vec_t rotated_vel = vec_rotate_quadrants_clockwise(collided_block->vel, portal_rotations);

               switch(collided_block_dir){
               default:
                    break;
               case DIRECTION_LEFT:
                    if(rotated_accel.x > 0.0f){
                         collided_block->pos -= collided_block_delta;
                         pos_delta -= pos_delta_diff;
                         rotated_accel.x = 0.0f;
                         rotated_vel.x = 0.0f;
                         collided_block->accel = vec_rotate_quadrants_counter_clockwise(rotated_accel, portal_rotations);
                         collided_block->vel = vec_rotate_quadrants_counter_clockwise(rotated_vel, portal_rotations);
                    }
                    break;
               case DIRECTION_RIGHT:
                    if(rotated_accel.x < 0.0f){
                         collided_block->pos -= collided_block_delta;
                         pos_delta -= pos_delta_diff;
                         rotated_accel.x = 0.0f;
                         rotated_vel.x = 0.0f;
                         collided_block->accel = vec_rotate_quadrants_counter_clockwise(rotated_accel, portal_rotations);
                         collided_block->vel = vec_rotate_quadrants_counter_clockwise(rotated_vel, portal_rotations);
                    }
                    break;
               case DIRECTION_UP:
                    if(rotated_accel.y < 0.0f){
                         collided_block->pos -= collided_block_delta;
                         pos_delta -= pos_delta_diff;
                         rotated_accel.y = 0.0f;
                         rotated_vel.y = 0.0f;
                         collided_block->accel = vec_rotate_quadrants_counter_clockwise(rotated_accel, portal_rotations);
                         collided_block->vel = vec_rotate_quadrants_counter_clockwise(rotated_vel, portal_rotations);
                    }
                    break;
               case DIRECTION_DOWN:
                    if(rotated_accel.y > 0.0f){
                         collided_block->pos -= collided_block_delta;
                         pos_delta -= pos_delta_diff;
                         rotated_accel.y = 0.0f;
                         rotated_vel.y = 0.0f;
                         collided_block->accel = vec_rotate_quadrants_counter_clockwise(rotated_accel, portal_rotations);
                         collided_block->vel = vec_rotate_quadrants_counter_clockwise(rotated_vel, portal_rotations);
                    }
                    break;
               }
#endif

               Coord_t coord = pixel_to_coord(collided_block->pos.pixel + HALF_TILE_SIZE_PIXEL);
               Coord_t premove_coord = pixel_to_coord(pre_move.pixel + HALF_TILE_SIZE_PIXEL);

               Position_t block_center = collided_block->pos;
               block_center.pixel += HALF_TILE_SIZE_PIXEL;
               S8 rotations_between = teleport_position_across_portal(&block_center, NULL, world, premove_coord,
                                                                      coord);
               if(rotations_between >= 0){
                    collided_block->pos = block_center;
                    collided_block->pos.pixel -= HALF_TILE_SIZE_PIXEL;
               }

               auto rotated_player_face = direction_rotate_clockwise(player_face, portal_rotations);
               if(collided_block_dir == rotated_player_face && (player->accel.x != 0.0f || player->accel.y != 0.0f)){
                    if(*block_to_push == nullptr){ // also check that the player is actually pushing against the block
                         *block_to_push = world->blocks.elements + i;
                         *last_block_pushed_direction = player_face;
                    }else{
                         // stop the player from pushing 2 blocks at once
                         *block_to_push = nullptr;
                         *last_block_pushed_direction = DIRECTION_COUNT;
                    }
               }

               collided_blocks_mask_dir = direction_mask_add(collided_blocks_mask_dir, collided_block_dir);
               collided_blocks[collided_block_dir] = collided_block;
          }
     }

     Direction_t collided_tile_dir = DIRECTION_COUNT;
     for(S16 y = min.y; y <= max.y; y++){
          for(S16 x = min.x; x <= max.x; x++){
               if(world->tilemap.tiles[y][x].id){
                    Coord_t coord {x, y};
                    bool skip = false;
                    for(S16 d = 0; d < DIRECTION_COUNT; d++){
                         if(skip_coord[d] == coord){
                              skip = true;
                              break;
                         }
                    }
                    if(skip) continue;
                    bool collide_with_tile = false;
                    position_slide_against_rect(position, coord, PLAYER_RADIUS, &pos_delta, &collide_with_tile);
                    if(collide_with_tile) collided_tile_dir = direction_between(player_coord, coord);
               }
          }
     }

     Direction_t collided_interactive_dir = DIRECTION_COUNT;
     *collided_with_interactive = false;
     for(S16 y = min.y; y <= max.y; y++){
          for(S16 x = min.x; x <= max.x; x++){
               Coord_t coord {x, y};

               Interactive_t* interactive = quad_tree_interactive_solid_at(world->interactive_qt, &world->tilemap, coord);
               if(interactive){
                    bool collided = false;
                    position_slide_against_rect(position, coord, PLAYER_RADIUS, &pos_delta, &collided);
                    if(collided && !(*collided_with_interactive)){
                         *collided_with_interactive = true;
                         collided_interactive_dir = direction_between(player_coord, coord);
                    }
               }
          }
     }

     // TODO do we care about other directions for colliding with interactives and tiles like we do for blocks?

     // check if block is squishing the player against something
     for(S8 d = 0; d < DIRECTION_COUNT; d++){
          if(!collided_blocks[d]) continue;

          Direction_t dir = static_cast<Direction_t>(d);

#ifdef BLOCKS_SQUISH_PLAYER
          Direction_t opposite = direction_opposite(dir);
          DirectionMask_t block_vel_mask = vec_direction_mask(collided_blocks[dir]->vel);

          // ignore if the block is moving away
          if(direction_in_mask(block_vel_mask, dir)) continue;

          // if, on the opposite side of the collision, is a wall, interactive, or block, then kill the player muhahaha
          if((dir == direction_opposite(collided_interactive_dir) ||
              dir == direction_opposite(collided_tile_dir) ||
              direction_in_mask(collided_blocks_mask_dir, opposite))){
               *resetting = true;
               break;
          }
#else
          (void)(resetting);

          if(dir != DIRECTION_COUNT &&
             (dir == direction_opposite(collided_interactive_dir) ||
              dir == direction_opposite(collided_tile_dir))){
               switch(dir){
               default:
                    break;
               case DIRECTION_LEFT:
                    if(collided_blocks[d]->accel.x > 0){
                         collided_blocks[d]->pos -= collided_block_delta;
                         collided_blocks[d]->accel.x = 0.0f;
                         collided_blocks[d]->vel.x = 0.0f;
                    }
                    break;
               case DIRECTION_RIGHT:
                    if(collided_blocks[d]->accel.x < 0){
                         collided_blocks[d]->pos -= collided_block_delta;
                         collided_blocks[d]->accel.x = 0.0f;
                         collided_blocks[d]->vel.x = 0.0f;
                    }
                    break;
               case DIRECTION_DOWN:
                    if(collided_blocks[d]->accel.y > 0){
                         collided_blocks[d]->pos -= collided_block_delta;
                         collided_blocks[d]->accel.y = 0.0f;
                         collided_blocks[d]->vel.y = 0.0f;
                    }
                    break;
               case DIRECTION_UP:
                    if(collided_blocks[d]->accel.y < 0){
                         collided_blocks[d]->pos -= collided_block_delta;
                         collided_blocks[d]->accel.y = 0.0f;
                         collided_blocks[d]->vel.y = 0.0f;
                    }
               }
          }
#endif
     }

     return pos_delta;
}

S8 teleport_position_across_portal(Position_t* position, Vec_t* pos_delta, World_t* world, Coord_t premove_coord,
                                   Coord_t postmove_coord){
     if(postmove_coord != premove_coord){
          auto* interactive = quad_tree_interactive_find_at(world->interactive_qt, postmove_coord);
          if(is_active_portal(interactive)){
               if(interactive->portal.face == direction_opposite(direction_between(postmove_coord, premove_coord))){
                    Position_t offset_from_center = *position - coord_to_pos_at_tile_center(postmove_coord);
                    PortalExit_t portal_exit = find_portal_exits(postmove_coord, &world->tilemap, world->interactive_qt);

                    for(S8 d = 0; d < DIRECTION_COUNT; d++){
                         for(S8 p = 0; p < portal_exit.directions[d].count; p++){
                              if(portal_exit.directions[d].coords[p] != postmove_coord){
                                   Position_t final_offset = offset_from_center;
                                   Direction_t opposite = direction_opposite((Direction_t)(d));
                                   U8 rotations_between = direction_rotations_between(interactive->portal.face, opposite);

                                   final_offset = position_rotate_quadrants_counter_clockwise(final_offset, rotations_between);
                                   // final_offset.pixel = pixel_rotate_quadrants_counter_clockwise(final_offset.pixel, rotations_between);
                                   // final_offset.decimal = vec_rotate_quadrants_counter_clockwise(final_offset.decimal, rotations_between);

                                   rotations_between = portal_rotations_between(interactive->portal.face, (Direction_t)(d));

                                   if(pos_delta){
                                        *pos_delta = vec_rotate_quadrants_clockwise(*pos_delta, rotations_between);
                                   }

                                   *position = coord_to_pos_at_tile_center(portal_exit.directions[d].coords[p] + opposite) + final_offset;
                                   return rotations_between;
                              }
                         }
                    }
               }
          }
     }

     return -1;
}

static void illuminate_line(Coord_t start, Coord_t end, U8 value, World_t* world, Coord_t from_portal){
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
          Tile_t* tile = tilemap_get_tile(&world->tilemap, coords[i]);
          if(!tile) continue;

          S16 diff_x = abs(coords[i].x - start.x);
          S16 diff_y = abs(coords[i].y - start.y);
          U8 distance = static_cast<U8>(sqrt(static_cast<F32>(diff_x * diff_x + diff_y * diff_y)));

          U8 new_value = value - (distance * LIGHT_DECAY);

          if(coords[i] != from_portal){
               Interactive_t* interactive = quad_tree_interactive_find_at(world->interactive_qt, coords[i]);
               if(is_active_portal(interactive)){
                    PortalExit_t portal_exits = find_portal_exits(coords[i], &world->tilemap, world->interactive_qt);
                    for(S8 d = 0; d < DIRECTION_COUNT; d++){
                         for(S8 p = 0; p < portal_exits.directions[d].count; p++){
                              if(portal_exits.directions[d].coords[p] == coords[i]) continue;
                              illuminate(portal_exits.directions[d].coords[p], new_value, world,
                                         portal_exits.directions[d].coords[p]);
                         }
                    }
               }
          }

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
               quad_tree_find_in(world->block_qt, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

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

void illuminate(Coord_t coord, U8 value, World_t* world, Coord_t from_portal){
     if(coord.x < 0 || coord.y < 0 || coord.x >= world->tilemap.width || coord.y >= world->tilemap.height) return;

     S16 radius = ((value - BASE_LIGHT) / LIGHT_DECAY) + 1;

     if(radius < 0) return;

     Coord_t delta {radius, radius};
     Coord_t min = coord - delta;
     Coord_t max = coord + delta;

     for(S16 j = min.y + 1; j < max.y; ++j) {
          // bottom of box
          illuminate_line(coord, Coord_t{min.x, j}, value, world, from_portal);

          // top of box
          illuminate_line(coord, Coord_t{max.x, j}, value, world, from_portal);
     }

     for(S16 i = min.x + 1; i < max.x; ++i) {
          // left of box
          illuminate_line(coord, Coord_t{i, min.y,}, value, world, from_portal);

          // right of box
          illuminate_line(coord, Coord_t{i, max.y,}, value, world, from_portal);
     }
}

static void impact_ice(Coord_t center, S16 radius, World_t* world, bool teleported, bool spread_the_ice){
     Coord_t delta {radius, radius};
     Coord_t min = center - delta;
     Coord_t max = center + delta;

     for(S16 y = min.y; y <= max.y; ++y){
          for(S16 x = min.x; x <= max.x; ++x){
               Coord_t coord{x, y};
               Tile_t* tile = tilemap_get_tile(&world->tilemap, coord);
               if(tile && !tile_is_solid(tile)){
                    Rect_t coord_rect = rect_surrounding_adjacent_coords(coord);
                    S16 block_count = 0;
                    Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
                    quad_tree_find_in(world->block_qt, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

                    Block_t* block = nullptr;
                    for(S16 i = 0; i < block_count; i++){
                         if(block_get_coord(blocks[i]) == coord && blocks[i]->pos.z == 0){
                              block = blocks[i];
                              break;
                         }
                    }

                    Interactive_t* interactive = quad_tree_find_at(world->interactive_qt, coord.x, coord.y);

                    if(block){
                         if(spread_the_ice){
                              if(block->element == ELEMENT_NONE) block->element = ELEMENT_ONLY_ICED;
                         }else{
                              if(block->element == ELEMENT_ONLY_ICED) block->element = ELEMENT_NONE;
                         }
                    }else{
                         if(interactive){
                              // TODO: switch
                              if(interactive->type == INTERACTIVE_TYPE_POPUP){
                                   if(interactive->popup.lift.ticks == 1){
                                        if(spread_the_ice){
                                             interactive->popup.iced = false;
                                             tile->flags |= TILE_FLAG_ICED;
                                        }else{
                                             tile->flags &= ~TILE_FLAG_ICED;
                                        }
                                   }else{
                                        interactive->popup.iced = spread_the_ice;
                                   }
                              }else if(interactive->type == INTERACTIVE_TYPE_PRESSURE_PLATE ||
                                       interactive->type == INTERACTIVE_TYPE_ICE_DETECTOR ||
                                       interactive->type == INTERACTIVE_TYPE_LIGHT_DETECTOR){
                                   if(spread_the_ice){
                                        tile->flags |= TILE_FLAG_ICED;
                                   }else{
                                        tile->flags &= ~TILE_FLAG_ICED;
                                        if(interactive->type == INTERACTIVE_TYPE_PRESSURE_PLATE){
                                             interactive->pressure_plate.iced_under = false;
                                        }
                                   }
                              }
                         }else{
                              if(spread_the_ice){
                                   tile->flags |= TILE_FLAG_ICED;
                              }else{
                                   tile->flags &= ~TILE_FLAG_ICED;
                              }
                         }
                    }

                    if(is_active_portal(interactive) && !teleported){
                         auto portal_exits = find_portal_exits(coord, &world->tilemap, world->interactive_qt);
                         for(S8 d = 0; d < DIRECTION_COUNT; d++){
                              for(S8 p = 0; p < portal_exits.directions[d].count; p++){
                                   if(portal_exits.directions[d].coords[p] == coord) continue;
                                   S16 x_diff = coord.x - center.x;
                                   S16 y_diff = coord.y - center.y;
                                   U8 distance_from_center = (U8)(sqrt(x_diff * x_diff + y_diff * y_diff));
                                   Direction_t opposite = direction_opposite((Direction_t)(d));

                                   impact_ice(portal_exits.directions[d].coords[p] + opposite, radius - distance_from_center,
                                              world, true, spread_the_ice);
                              }
                         }
                    }
               }
          }
     }
}

void spread_ice(Coord_t center, S16 radius, World_t* world, bool teleported){
     impact_ice(center, radius, world, teleported, true);
}

void melt_ice(Coord_t center, S16 radius, World_t* world, bool teleported){
     impact_ice(center, radius, world, teleported, false);
}

void describe_coord(Coord_t coord, World_t* world){
     LOG("\ndescribe_coord(%d, %d)\n", coord.x, coord.y);
     auto* tile = tilemap_get_tile(&world->tilemap, coord);
     if(tile){
          LOG("Tile: id: %u, light: %u\n", tile->id, tile->light);
          if(tile->flags){
               LOG(" flags:\n");
               if(tile->flags & TILE_FLAG_ICED) printf("  ICED\n");
               if(tile->flags & TILE_FLAG_CHECKPOINT) printf("  CHECKPOINT\n");
               if(tile->flags & TILE_FLAG_RESET_IMMUNE) printf("  RESET_IMMUNE\n");
               if(tile->flags & TILE_FLAG_WIRE_STATE) printf("  WIRE_STATE\n");
               if(tile->flags & TILE_FLAG_WIRE_LEFT) printf("  WIRE_LEFT\n");
               if(tile->flags & TILE_FLAG_WIRE_UP) printf("  WIRE_UP\n");
               if(tile->flags & TILE_FLAG_WIRE_RIGHT) printf("  WIRE_RIGHT\n");
               if(tile->flags & TILE_FLAG_WIRE_DOWN) printf("  WIRE_DOWN\n");
               if(tile->flags & TILE_FLAG_WIRE_CLUSTER_LEFT) printf("  CLUSTER_LEFT\n");
               if(tile->flags & TILE_FLAG_WIRE_CLUSTER_MID) printf("  CLUSTER_MID\n");
               if(tile->flags & TILE_FLAG_WIRE_CLUSTER_RIGHT) printf("  CLUSTER_RIGHT\n");
               if(tile->flags & TILE_FLAG_WIRE_CLUSTER_LEFT_ON) printf("  CLUSTER_LEFT_ON\n");
               if(tile->flags & TILE_FLAG_WIRE_CLUSTER_MID_ON) printf("  CLUSTER_MID_ON\n");
               if(tile->flags & TILE_FLAG_WIRE_CLUSTER_RIGHT_ON) printf("  CLUSTER_RIGHT_ON\n");
          }
     }

     auto* interactive = quad_tree_find_at(world->interactive_qt, coord.x, coord.y);
     if(interactive){
          const char* type_string = "INTERACTIVE_TYPE_UKNOWN";
          const int info_string_len = 32;
          char info_string[info_string_len];
          memset(info_string, 0, info_string_len);

          switch(interactive->type){
          default:
               break;
          case INTERACTIVE_TYPE_NONE:
               type_string = "NONE";
               break;
          case INTERACTIVE_TYPE_PRESSURE_PLATE:
               type_string = "PRESSURE_PLATE";
               snprintf(info_string, info_string_len, "down: %d, iced_undo: %d",
                        interactive->pressure_plate.down, interactive->pressure_plate.iced_under);
               break;
          case INTERACTIVE_TYPE_LIGHT_DETECTOR:
               type_string = "LIGHT_DETECTOR";
               snprintf(info_string, info_string_len, "on: %d", interactive->detector.on);
               break;
          case INTERACTIVE_TYPE_ICE_DETECTOR:
               type_string = "ICE_DETECTOR";
               snprintf(info_string, info_string_len, "on: %d", interactive->detector.on);
               break;
          case INTERACTIVE_TYPE_POPUP:
               type_string = "POPUP";
               snprintf(info_string, info_string_len, "lift: ticks: %u, up: %d, iced: %d", interactive->popup.lift.ticks, interactive->popup.lift.up, interactive->popup.iced);
               break;
          case INTERACTIVE_TYPE_LEVER:
               type_string = "LEVER";
               break;
          case INTERACTIVE_TYPE_DOOR:
               type_string = "DOOR";
               snprintf(info_string, info_string_len, "face: %s, lift: ticks %u, up: %d",
                        direction_to_string(interactive->door.face), interactive->door.lift.ticks,
                        interactive->door.lift.up);
               break;
          case INTERACTIVE_TYPE_PORTAL:
               type_string = "PORTAL";
               snprintf(info_string, info_string_len, "face: %s, on: %d", direction_to_string(interactive->portal.face),
                        interactive->portal.on);
               break;
          case INTERACTIVE_TYPE_BOMB:
               type_string = "BOMB";
               break;
          case INTERACTIVE_TYPE_BOW:
               type_string = "BOW";
               break;
          case INTERACTIVE_TYPE_STAIRS:
               type_string = "STAIRS";
               break;
          case INTERACTIVE_TYPE_PROMPT:
               type_string = "PROMPT";
               break;
          }

          LOG("type: %s %s\n", type_string, info_string);
     }

     Rect_t coord_rect;

     coord_rect.left = coord.x * TILE_SIZE_IN_PIXELS;
     coord_rect.bottom = coord.y * TILE_SIZE_IN_PIXELS;
     coord_rect.right = coord_rect.left + TILE_SIZE_IN_PIXELS;
     coord_rect.top = coord_rect.bottom + TILE_SIZE_IN_PIXELS;

     S16 block_count = 0;
     Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
     quad_tree_find_in(world->block_qt, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);
     for(S16 i = 0; i < block_count; i++){
          auto* block = blocks[i];
          LOG("block: %d, %d, dir: %s, element: %s\n", block->pos.pixel.x, block->pos.pixel.y,
              direction_to_string(block->face), element_to_string(block->element));
     }
}