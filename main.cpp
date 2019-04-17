/*
http://www.simonstalenhag.se/
-Grant Sanderson 3blue1brown
-Shane Hendrixson

TODO:
Entanglement Puzzles:
- entangle puzzle where there is a line of pressure plates against the wall with a line of popups on the other side that would
  trap an entangled block if it got to close, stopping the player from using walls to get blocks closer
- entangle puzzle with an extra non-entangled block where you don't want one of the entangled blocks to move, and you use
  the non-entangled block to accomplish that
- rotated entangled puzzles where the centroid is on a portal destination coord

Current bugs:
- a block on the tile outside a portal pushed into the portal to clone, the clone has weird behavior and ends up on the portal block
- when pushing a block through a portal that turns off, the block keeps going
- getting a block and it's rotated entangler to push into the centroid causes the any other entangled blocks to alternate pushing
- lockups/crashing when lots of entangled blocks collided on ice
- In the case of 3 rotated entangled blocks, 2 blocks that have a rotation of 2 between them colliding into each other seem to end up off of the grid

Current Design issues:
- it is important for the player to force a rotated entangled block to not move when we push the other block. To do this we have to wedge ourself against it before pushing the block we want.
  if you forget to do this the block will be slightly off grid which isn't great

Big Features:
- 3D
     - note: only 2 blocks high can go through portals
     - shadows and slightly discolored blocks should help with visualizations
- arrow kills player
- arrow entanglement
- Block splitting
- Bring block ice collision to the masses

*/

#include <iostream>
#include <chrono>
#include <cfloat>
#include <cassert>
#include <thread>

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "log.h"
#include "defines.h"
#include "direction.h"
#include "bitmap.h"
#include "conversion.h"
#include "object_array.h"
#include "portal_exit.h"
#include "block.h"
#include "utils.h"
#include "map_format.h"
#include "draw.h"
#include "block_utils.h"
#include "demo.h"
#include "collision.h"
#include "world.h"
#include "editor.h"
#include "utils.h"

#define DEBUG_FILE 0

struct VecMaskCollisionEntry_t{
     S8 mask;

     // how a and b have to move in order to collide

     // first scenario
     Direction_t move_a_1;
     Direction_t move_b_1;

     // second scenario
     Direction_t move_a_2;
     Direction_t move_b_2;
};

FILE* load_demo_number(S32 map_number, const char** demo_filepath){
     char filepath[64] = {};
     snprintf(filepath, 64, "content/%03d.bd", map_number);
     *demo_filepath = strdup(filepath);
     return fopen(*demo_filepath, "rb");
}

void cache_for_demo_seek(World_t* world, TileMap_t* demo_starting_tilemap, ObjectArray_t<Block_t>* demo_starting_blocks,
                         ObjectArray_t<Interactive_t>* demo_starting_interactives){
     deep_copy(&world->tilemap, demo_starting_tilemap);
     deep_copy(&world->blocks, demo_starting_blocks);
     deep_copy(&world->interactives, demo_starting_interactives);
}

void fetch_cache_for_demo_seek(World_t* world, TileMap_t* demo_starting_tilemap, ObjectArray_t<Block_t>* demo_starting_blocks,
                               ObjectArray_t<Interactive_t>* demo_starting_interactives){
     deep_copy(demo_starting_tilemap, &world->tilemap);
     deep_copy(demo_starting_blocks, &world->blocks);
     deep_copy(demo_starting_interactives, &world->interactives);
}

bool load_map_number_demo(Demo_t* demo, S16 map_number, S64* frame_count){
     if(demo->file) fclose(demo->file);
     demo->file = load_demo_number(map_number, &demo->filepath);
     if(!demo->file){
          LOG("missing map %d corresponding demo.\n", map_number);
          return false;
     }

     free(demo->entries.entries);
     demo->entry_index = 0;
     fread(&demo->version, sizeof(demo->version), 1, demo->file);
     demo->entries = demo_entries_get(demo->file);
     *frame_count = 0;
     demo->last_frame = demo->entries.entries[demo->entries.count - 1].frame;
     LOG("testing demo %s: version %d with %ld actions across %ld frames\n", demo->filepath,
         demo->version, demo->entries.count, demo->last_frame);
     return true;
}

bool load_map_number_map(S16 map_number, World_t* world, Undo_t* undo,
                         Coord_t* player_start, PlayerAction_t* player_action){
     if(load_map_number(map_number, player_start, world)){
          reset_map(*player_start, world, undo);
          *player_action = {};
          return true;
     }

     return false;
}

void update_light_and_ice_detectors(Interactive_t* interactive, World_t* world){
     switch(interactive->type){
     default:
          break;
     case INTERACTIVE_TYPE_LIGHT_DETECTOR:
     {
          Tile_t* tile = tilemap_get_tile(&world->tilemap, interactive->coord);
          Rect_t coord_rect = rect_surrounding_adjacent_coords(interactive->coord);

          S16 block_count = 0;
          Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
          quad_tree_find_in(world->block_qt, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

          Block_t* block = nullptr;
          for(S16 b = 0; b < block_count; b++){
               // blocks on the coordinate and on the ground block light
               if(block_get_coord(blocks[b]) == interactive->coord && blocks[b]->pos.z == 0){
                    block = blocks[b];
                    break;
               }
          }

          if(interactive->detector.on && (tile->light < LIGHT_DETECTOR_THRESHOLD || block)){
               activate(world, interactive->coord);
               interactive->detector.on = false;
          }else if(!interactive->detector.on && tile->light >= LIGHT_DETECTOR_THRESHOLD && !block){
               activate(world, interactive->coord);
               interactive->detector.on = true;
          }
          break;
     }
     case INTERACTIVE_TYPE_ICE_DETECTOR:
     {
          Tile_t* tile = tilemap_get_tile(&world->tilemap, interactive->coord);
          if(tile){
               if(interactive->detector.on && !tile_is_iced(tile)){
                    activate(world, interactive->coord);
                    interactive->detector.on = false;
               }else if(!interactive->detector.on && tile_is_iced(tile)){
                    activate(world, interactive->coord);
                    interactive->detector.on = true;
               }
          }
          break;
     }
     }
}

void stop_block_colliding_with_entangled(Block_t* block, Direction_t move_dir_to_stop, CheckBlockCollisionResult_t* result){
     switch(move_dir_to_stop){
     default:
          break;
     case DIRECTION_LEFT:
     case DIRECTION_RIGHT:
          block->pos_delta.x = 0;
          block->pos_delta.y = result->pos_delta.y;
          block->vel.x = 0;
          block->vel.y = result->vel.y;
          block->accel.x = 0;
          block->accel.y = result->accel.y;

          block->stop_on_pixel_x = 0;

          reset_move(&block->horizontal_move);
          block->vertical_move = result->vertical_move;
          break;
     case DIRECTION_UP:
     case DIRECTION_DOWN:
          block->pos_delta.x = result->pos_delta.x;
          block->pos_delta.y = 0;
          block->vel.x = result->vel.x;
          block->vel.y = 0;
          block->accel.x = result->accel.x;
          block->accel.y = 0;

          block->stop_on_pixel_y = 0;

          block->horizontal_move = result->horizontal_move;
          reset_move(&block->vertical_move);
          break;
     }

     switch(move_dir_to_stop){
     default:
          break;
     case DIRECTION_LEFT:
          if(block->pos.decimal.x > 0) block->pos.pixel.x += 1;
     case DIRECTION_RIGHT:
          block->pos.decimal.x = 0;
          break;
     case DIRECTION_DOWN:
          if(block->pos.decimal.y > 0) block->pos.pixel.y += 1;
     case DIRECTION_UP:
          block->pos.decimal.y = 0;
          break;
     }
}

bool check_direction_from_block_for_adjacent_walls(Block_t* block, TileMap_t* tilemap, QuadTreeNode_t<Interactive_t>* interactive_qt,
                                                   Coord_t* skip_coords, Direction_t direction){
     Pixel_t pixel_a {};
     Pixel_t pixel_b {};
     block_adjacent_pixels_to_check(block->pos, block->pos_delta, direction, &pixel_a, &pixel_b);
     Coord_t coord_a = pixel_to_coord(pixel_a);
     Coord_t coord_b = pixel_to_coord(pixel_b);

     if(coord_a != skip_coords[direction] && tilemap_is_solid(tilemap, coord_a)){
          return true;
     }else if(coord_b != skip_coords[direction] && tilemap_is_solid(tilemap, coord_b)){
          return true;
     }

     Interactive_t* a = quad_tree_interactive_solid_at(interactive_qt, tilemap, coord_a);
     Interactive_t* b = quad_tree_interactive_solid_at(interactive_qt, tilemap, coord_b);

     if(a){
          if(a->type == INTERACTIVE_TYPE_POPUP &&
             (a->popup.lift.ticks - 1) <= block->pos.z){
          }else{
               return true;
          }
     }

     if(b){
          if(b->type == INTERACTIVE_TYPE_POPUP &&
             (b->popup.lift.ticks - 1) <= block->pos.z){
          }else{
               return true;
          }
     }

     return false;
}

void copy_block_collision_results(Block_t* block, CheckBlockCollisionResult_t* result){
     block->pos_delta = result->pos_delta;
     block->vel = result->vel;
     block->accel = result->accel;

     block->stop_on_pixel_x = result->stop_on_pixel_x;
     block->stop_on_pixel_y = result->stop_on_pixel_y;

     block->horizontal_move = result->horizontal_move;
     block->vertical_move = result->vertical_move;
}

int block_height_comparer(const void* a, const void* b){
    Block_t* real_a = *(Block_t**)(a);
    Block_t* real_b = *(Block_t**)(b);

    return real_a->pos.z > real_b->pos.z;
}

void sort_blocks_by_height(Block_t** blocks, S16 block_count){
    qsort(blocks, block_count, sizeof(*blocks), block_height_comparer);
}

int main(int argc, char** argv){
     const char* load_map_filepath = nullptr;
     bool test = false;
     bool suite = false;
     bool show_suite = false;
     S16 map_number = 0;
     S16 first_map_number = 0;
     S16 first_frame = 0;

     Demo_t demo {};

     for(int i = 1; i < argc; i++){
          if(strcmp(argv[i], "-play") == 0){
               int next = i + 1;
               if(next >= argc) continue;
               demo.filepath = argv[next];
               demo.mode = DEMO_MODE_PLAY;
          }else if(strcmp(argv[i], "-record") == 0){
               int next = i + 1;
               if(next >= argc) continue;
               demo.filepath = argv[next];
               demo.mode = DEMO_MODE_RECORD;
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
               map_number = (S16)(atoi(argv[next]));
               first_map_number = map_number;
          }else if(strcmp(argv[i], "-frame") == 0){
               int next = i + 1;
               if(next >= argc) continue;
               first_frame = (S16)(atoi(argv[next]));
          }else if(strcmp(argv[i], "-speed") == 0){
               int next = i + 1;
               if(next >= argc) continue;
               demo.dt_scalar = (F32)(atof(argv[next]));
          }else if(strcmp(argv[i], "-h") == 0){
               printf("%s [options]\n", argv[0]);
               printf("  -play   <demo filepath> replay a recorded demo file\n");
               printf("  -record <demo filepath> record a demo file\n");
               printf("  -load   <map filepath>  load a map\n");
               printf("  -test                   validate the map state is correct after playing a demo\n");
               printf("  -suite                  run map/demo combos in succession validating map state after each headless\n");
               printf("  -show                   use in combination with -suite to run with a head\n");
               printf("  -map    <integer>       load a map by number\n");
               printf("  -speed  <decimal>       when replaying a demo, specify how fast/slow to replay where 1.0 is realtime\n");
               printf("  -frame  <integer>       which frame to play to automatically before drawing\n");
               printf("  -h this help.\n");
               return 0;
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

     int window_width = 1024;
     int window_height = 1024;
     SDL_Window* window = nullptr;
     SDL_GLContext opengl_context = nullptr;
     GLuint theme_texture = 0;
     GLuint player_texture = 0;
     GLuint arrow_texture = 0;
     GLuint text_texture = 0;

     if(!suite || show_suite){
          if(SDL_Init(SDL_INIT_EVERYTHING) != 0){
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

          // SDL_GL_SetSwapInterval(SDL_TRUE);
          SDL_GL_SetSwapInterval(SDL_FALSE);
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
          text_texture = transparent_texture_from_file("content/text.bmp");
          if(text_texture == 0) return 1;
     }

     if(demo.mode != DEMO_MODE_NONE){
          if(!demo_begin(&demo)){
               return 1;
          }
     }

     World_t world {};

     Editor_t editor {};
     Undo_t undo {};

     Coord_t player_start {2, 8};

     S64 frame_count = 0;

     bool quit = false;
     bool seeked_with_mouse = false;
     bool resetting = false;
     F32 reset_timer = 1.0f;

     PlayerAction_t player_action {};
     Position_t camera = coord_to_pos(Coord_t{8, 8});

     Vec_t mouse_screen = {}; // 0.0f to 1.0f
     Position_t mouse_world = {};
     bool ctrl_down = false;

     // cached to seek in demo faster
     TileMap_t demo_starting_tilemap {};
     ObjectArray_t<Block_t> demo_starting_blocks {};
     ObjectArray_t<Interactive_t> demo_starting_interactives {};

     Quad_t pct_bar_outline_quad = {0, 2.0f * PIXEL_SIZE, 1.0f, 0.02f};

     if(load_map_filepath){
          if(!load_map(load_map_filepath, &player_start, &world.tilemap, &world.blocks, &world.interactives)){
               return 1;
          }

          if(demo.mode == DEMO_MODE_PLAY){
               cache_for_demo_seek(&world, &demo_starting_tilemap, &demo_starting_blocks, &demo_starting_interactives);
          }
     }else if(suite){
          if(!load_map_number(map_number, &player_start, &world)){
               return 1;
          }

          cache_for_demo_seek(&world, &demo_starting_tilemap, &demo_starting_blocks, &demo_starting_interactives);

          demo.mode = DEMO_MODE_PLAY;
          if(!load_map_number_demo(&demo, map_number, &frame_count)){
               return 1;
          }
     }else if(map_number){
          if(!load_map_number(map_number, &player_start, &world)){
               return 1;
          }

          if(demo.mode == DEMO_MODE_PLAY){
               cache_for_demo_seek(&world, &demo_starting_tilemap, &demo_starting_blocks, &demo_starting_interactives);
          }

          if(first_frame > 0 && first_frame < demo.last_frame){
               demo.seek_frame = first_frame;
               demo.paused = true;
          }
     }else{
          setup_default_room(&world);
     }

     reset_map(player_start, &world, &undo);
     init(&editor);

#if DEBUG_FILE
     FILE* debug_file = fopen("debug.txt", "w");
#endif

     F32 dt = 0.0f;

     auto last_time = std::chrono::system_clock::now();
     auto current_time = last_time;

     while(!quit){
          if((!suite || show_suite) && demo.seek_frame < 0){
               current_time = std::chrono::system_clock::now();
               std::chrono::duration<double> elapsed_seconds = current_time - last_time;
               auto elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_time);
               dt = (F32)(elapsed_seconds.count());

               if(dt < 0.0166666f / demo.dt_scalar){
                    if(elapsed_milliseconds.count() < 16){
                         std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    continue; // limit 60 fps
               }
          }

          last_time = current_time;

          // TODO: consider 30fps as minimum for random noobs computers
          // if(demo.mode) dt = 0.0166666f; // the game always runs as if a 60th of a frame has occurred.
          dt = 0.0166666f; // the game always runs as if a 60th of a frame has occurred.

          quad_tree_free(world.block_qt);
          world.block_qt = quad_tree_build(&world.blocks);

          if(!demo.paused || demo.seek_frame >= 0){
               frame_count++;
               if(demo.seek_frame == frame_count) demo.seek_frame = -1;
          }

          player_action.last_activate = player_action.activate;
          for(S16 i = 0; i < world.players.count; i++) {
               world.players.elements[i].reface = false;
          }

          if(demo.mode == DEMO_MODE_PLAY){
               if(demo_play_frame(&demo, &player_action, &world.players, frame_count)){
                    if(test){
                         if(!test_map_end_state(&world, &demo)){
                              LOG("test failed\n");
                              demo.mode = DEMO_MODE_NONE;
                              if(suite && !show_suite) return 1;
                         }else if(suite){
                              map_number++;
                              S16 maps_tested = map_number - first_map_number;

                              if(load_map_number_map(map_number, &world, &undo, &player_start, &player_action)){
                                   cache_for_demo_seek(&world, &demo_starting_tilemap, &demo_starting_blocks, &demo_starting_interactives);

                                   if(load_map_number_demo(&demo, map_number, &frame_count)){
                                        continue; // reset to the top of the loop
                                   }else{
                                        return 1;
                                   }
                              }else{
                                   LOG("Done Testing %d maps.\n", maps_tested);
                                   return 0;
                              }
                         }
                    }else{
                         demo.paused = true;
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
                    case SDL_SCANCODE_A:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              move_selection(&editor, DIRECTION_LEFT);
                         }else if(demo.mode == DEMO_MODE_PLAY){
                              if(frame_count > 0 && demo.seek_frame < 0){
                                   demo.seek_frame = frame_count - 1;

                                   // TODO: compress with same comment elsewhere in this file
                                   fetch_cache_for_demo_seek(&world, &demo_starting_tilemap, &demo_starting_blocks, &demo_starting_interactives);

                                   reset_map(player_start, &world, &undo);

                                   // reset some vars
                                   player_action = {};

                                   demo.entry_index = 0;
                                   frame_count = 0;
                              }
                         }else if(!resetting){
                              player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_MOVE_LEFT_START,
                                                    demo.mode, demo.file, frame_count);
                         }
                         break;
                    case SDL_SCANCODE_RIGHT:
                    case SDL_SCANCODE_D:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              move_selection(&editor, DIRECTION_RIGHT);
                         }else if(demo.mode == DEMO_MODE_PLAY){
                              if(demo.seek_frame < 0){
                                   demo.seek_frame = frame_count + 1;
                              }
                         }else if(!resetting){
                              player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_MOVE_RIGHT_START,
                                                    demo.mode, demo.file, frame_count);
                         }
                         break;
                    case SDL_SCANCODE_UP:
                    case SDL_SCANCODE_W:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              move_selection(&editor, DIRECTION_UP);
                         }else if(!resetting){
                              player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_MOVE_UP_START,
                                                    demo.mode, demo.file, frame_count);
                         }
                         break;
                    case SDL_SCANCODE_DOWN:
                    case SDL_SCANCODE_S:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              move_selection(&editor, DIRECTION_DOWN);
                         }else if(!resetting){
                              player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_MOVE_DOWN_START,
                                                    demo.mode, demo.file, frame_count);
                         }
                         break;
                    case SDL_SCANCODE_E:
                         if(!resetting){
                              player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_ACTIVATE_START,
                                                    demo.mode, demo.file, frame_count);
                         }
                         break;
                    case SDL_SCANCODE_SPACE:
                         if(demo.mode == DEMO_MODE_PLAY){
                              demo.paused = !demo.paused;
                         }else{
                              if(!resetting){
                                   player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_SHOOT_START,
                                                         demo.mode, demo.file, frame_count);
                              }
                         }
                         break;
                    case SDL_SCANCODE_L:
                         if(load_map_number_map(map_number, &world, &undo, &player_start, &player_action)){
                              if(demo.mode == DEMO_MODE_PLAY){
                                   cache_for_demo_seek(&world, &demo_starting_tilemap, &demo_starting_blocks, &demo_starting_interactives);
                              }
                         }
                         break;
                    case SDL_SCANCODE_LEFTBRACKET:
                         map_number--;
                         if(load_map_number_map(map_number, &world, &undo, &player_start, &player_action)){
                              if(demo.mode == DEMO_MODE_PLAY){
                                   cache_for_demo_seek(&world, &demo_starting_tilemap, &demo_starting_blocks, &demo_starting_interactives);

                                   if(load_map_number_demo(&demo, map_number, &frame_count)){
                                        continue; // reset to the top of the loop
                                   }else{
                                        return 1;
                                   }
                              }
                         }else{
                              map_number++;
                         }
                         break;
                    case SDL_SCANCODE_RIGHTBRACKET:
                         map_number++;
                         if(load_map_number_map(map_number, &world, &undo, &player_start, &player_action)){
                              if(demo.mode == DEMO_MODE_PLAY){
                                   cache_for_demo_seek(&world, &demo_starting_tilemap, &demo_starting_blocks, &demo_starting_interactives);

                                   if(load_map_number_demo(&demo, map_number, &frame_count)){
                                        continue; // reset to the top of the loop
                                   }else{
                                        return 1;
                                   }
                              }
                         }else{
                              map_number--;
                         }
                         break;
                    case SDL_SCANCODE_MINUS:
                         if(demo.dt_scalar > 0.1f){
                              demo.dt_scalar -= 0.1f;
                              LOG("game dt scalar: %.1f\n", demo.dt_scalar);
                         }
                         break;
                    case SDL_SCANCODE_EQUALS:
                         demo.dt_scalar += 0.1f;
                         LOG("game dt scalar: %.1f\n", demo.dt_scalar);
                         break;
                    case SDL_SCANCODE_V:
                         if(editor.mode != EDITOR_MODE_OFF){
                              char filepath[64];
                              snprintf(filepath, 64, "content/%03d.bm", map_number);
                              save_map(filepath, player_start, &world.tilemap, &world.blocks, &world.interactives);
                         }
                         break;
                    case SDL_SCANCODE_U:
                         if(!resetting){
                              player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_UNDO,
                                                    demo.mode, demo.file, frame_count);
                         }
                         break;
                    case SDL_SCANCODE_N:
                    {
                         Tile_t* tile = tilemap_get_tile(&world.tilemap, mouse_select_world(mouse_screen, camera));
                         if(tile) tile_toggle_wire_activated(tile);
                    } break;
                    case SDL_SCANCODE_8:
                         if(editor.mode == EDITOR_MODE_CATEGORY_SELECT){
                              auto coord = mouse_select_world(mouse_screen, camera);
                              auto rect = rect_surrounding_coord(coord);

                              S16 block_count = 0;
                              Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
                              quad_tree_find_in(world.block_qt, rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

                              if(block_count > 1){
                                   LOG("error: too man blocks in coord, unsure which one to entangle!\\n");
                              }else if(block_count == 1){
                                   S16 block_index = (S16)(blocks[0] - world.blocks.elements);
                                   if(block_index >= 0 && block_index < world.blocks.count){
                                        if(editor.block_entangle_index_save >= 0 && editor.block_entangle_index_save != block_index){
                                             undo_commit(&undo, &world.players, &world.tilemap, &world.blocks, &world.interactives);

                                             // the magic happens here
                                             Block_t* other = world.blocks.elements + editor.block_entangle_index_save;

                                             // TODO: Probably not what we want in the future when things are
                                             //       circularly entangled, but fine for now
                                             if(other->entangle_index >= 0){
                                                  Block_t* other_other = world.blocks.elements + other->entangle_index;
                                                  other_other->entangle_index = -1;
                                             }

                                             other->entangle_index = block_index;
                                             blocks[0]->entangle_index = editor.block_entangle_index_save;

                                             // reset once we are done
                                             editor.block_entangle_index_save = -1;
                                             LOG("editor: entangled: %d <-> %d\n", blocks[0]->entangle_index, block_index);
                                        }else{
                                             editor.block_entangle_index_save = block_index;
                                             LOG("editor: entangle index save: %d\n", block_index);
                                        }
                                   }
                              }else if(block_count == 0){
                                   LOG("editor: clear entangle index save (was %d)\n", editor.block_entangle_index_save);
                                   editor.block_entangle_index_save = -1;
                              }
                         }
                         break;
                    case SDL_SCANCODE_2:
                         if(editor.mode == EDITOR_MODE_CATEGORY_SELECT){
                              auto pixel = mouse_select_world_pixel(mouse_screen, camera);

                              // TODO: make this a function where you can pass in entangle_index
                              S16 new_index = world.players.count;
                              if(resize(&world.players, world.players.count + (S16)(1))){
                                   Player_t* new_player = world.players.elements + new_index;
                                   *new_player = world.players.elements[0];
                                   new_player->pos = pixel_to_pos(pixel);
                              }
                         }
                         break;
                    case SDL_SCANCODE_0:
                         if(editor.mode == EDITOR_MODE_CATEGORY_SELECT){
                              for(S16 i = 0; i < world.players.count; i++){
                                   describe_player(&world, world.players.elements + i);
                              }

                              for(S16 i = 0; i < world.blocks.count; i++){
                                   describe_block(&world, world.blocks.elements + i);
                              }
                         }
                         break;
                    // TODO: #ifdef DEBUG
                    case SDL_SCANCODE_GRAVE:
                         if(editor.mode == EDITOR_MODE_OFF){
                              editor.mode = EDITOR_MODE_CATEGORY_SELECT;
                         }else{
                              editor.mode = EDITOR_MODE_OFF;
                              editor.selection_start = {};
                              editor.selection_end = {};
                              editor.block_entangle_index_save = -1;
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
                              undo_commit(&undo, &world.players, &world.tilemap, &world.blocks, &world.interactives);

                              // clear coords below stamp
                              Rect_t selection_bounds = editor_selection_bounds(&editor);
                              for(S16 j = selection_bounds.bottom; j <= selection_bounds.top; j++){
                                   for(S16 i = selection_bounds.left; i <= selection_bounds.right; i++){
                                        Coord_t coord {i, j};
                                        coord_clear(coord, &world.tilemap, &world.interactives, world.interactive_qt, &world.blocks);
                                   }
                              }

                              for(int i = 0; i < editor.selection.count; i++){
                                   Coord_t coord = editor.selection_start + editor.selection.elements[i].offset;
                                   apply_stamp(editor.selection.elements + i, coord,
                                               &world.tilemap, &world.blocks, &world.interactives, &world.interactive_qt, ctrl_down);
                              }

                              editor.mode = EDITOR_MODE_CATEGORY_SELECT;
                         }
                         break;
                    case SDL_SCANCODE_T:
                         if(editor.mode == EDITOR_MODE_SELECTION_MANIPULATION){
                              sort_selection(&editor);

                              S16 height_offset = (S16)((editor.selection_end.y - editor.selection_start.y) - 1);

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
                         }else if(editor.mode == EDITOR_MODE_OFF){
                              resetting = true;
                         }
                         break;
                    case SDL_SCANCODE_LCTRL:
                         ctrl_down = true;
                         break;
                    case SDL_SCANCODE_5:
                    {
                         reset_players(&world.players);
                         Player_t* player = world.players.elements;
                         player->pos.pixel = mouse_select_world_pixel(mouse_screen, camera) + HALF_TILE_SIZE_PIXEL;
                         player->pos.decimal.x = 0;
                         player->pos.decimal.y = 0;
                         break;
                    }
                    case SDL_SCANCODE_H:
                    {
                         Pixel_t pixel = mouse_select_world_pixel(mouse_screen, camera) + HALF_TILE_SIZE_PIXEL;
                         Coord_t coord = mouse_select_world(mouse_screen, camera);
                         LOG("mouse pixel: %d, %d, Coord: %d, %d\n", pixel.x, pixel.y, coord.x, coord.y);
                         describe_coord(coord, &world);
                    } break;
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
                    case SDL_SCANCODE_A:
                         if(demo.mode == DEMO_MODE_PLAY) break;
                         if(resetting) break;

                         player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_MOVE_LEFT_STOP,
                                               demo.mode, demo.file, frame_count);
                         break;
                    case SDL_SCANCODE_RIGHT:
                    case SDL_SCANCODE_D:
                         if(demo.mode == DEMO_MODE_PLAY) break;
                         if(resetting) break;

                         player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_MOVE_RIGHT_STOP,
                                               demo.mode, demo.file, frame_count);
                         break;
                    case SDL_SCANCODE_UP:
                    case SDL_SCANCODE_W:
                         if(demo.mode == DEMO_MODE_PLAY) break;
                         if(resetting) break;

                         player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_MOVE_UP_STOP,
                                               demo.mode, demo.file, frame_count);
                         break;
                    case SDL_SCANCODE_DOWN:
                    case SDL_SCANCODE_S:
                         if(demo.mode == DEMO_MODE_PLAY) break;
                         if(resetting) break;

                         player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_MOVE_DOWN_STOP,
                                               demo.mode, demo.file, frame_count);
                         break;
                    case SDL_SCANCODE_E:
                         if(demo.mode == DEMO_MODE_PLAY) break;
                         if(resetting) break;

                         player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_ACTIVATE_STOP,
                                               demo.mode, demo.file, frame_count);
                         break;
                    case SDL_SCANCODE_SPACE:
                         if(demo.mode == DEMO_MODE_PLAY) break;
                         if(resetting) break;

                         player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_SHOOT_STOP,
                                               demo.mode, demo.file, frame_count);
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
                         switch(editor.mode){
                         default:
                              break;
                         case EDITOR_MODE_OFF:
                              if(demo.mode == DEMO_MODE_PLAY){
                                   if(vec_in_quad(&pct_bar_outline_quad, mouse_screen)){
                                        seeked_with_mouse = true;

                                        demo.seek_frame = (S64)((F32)(demo.last_frame) * mouse_screen.x);

                                        if(demo.seek_frame < frame_count){
                                             // TODO: compress with same comment elsewhere in this file
                                             fetch_cache_for_demo_seek(&world, &demo_starting_tilemap, &demo_starting_blocks, &demo_starting_interactives);

                                             reset_map(player_start, &world, &undo);

                                             // reset some vars
                                             player_action = {};

                                             demo.entry_index = 0;
                                             frame_count = 0;
                                        }else if(demo.seek_frame == frame_count){
                                             demo.seek_frame = -1;
                                        }
                                   }
                              }
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
                                   undo_commit(&undo, &world.players, &world.tilemap, &world.blocks, &world.interactives);
                                   Coord_t select_coord = mouse_select_world(mouse_screen, camera);
                                   auto* stamp_array = editor.category_array.elements[editor.category].elements + editor.stamp;
                                   for(S16 s = 0; s < stamp_array->count; s++){
                                        auto* stamp = stamp_array->elements + s;
                                        apply_stamp(stamp, select_coord + stamp->offset,
                                                    &world.tilemap, &world.blocks, &world.interactives, &world.interactive_qt, ctrl_down);
                                   }

                                   quad_tree_free(world.block_qt);
                                   world.block_qt = quad_tree_build(&world.blocks);
                              }
                         } break;
                         }
                         break;
                    case SDL_BUTTON_RIGHT:
                         switch(editor.mode){
                         default:
                              break;
                         case EDITOR_MODE_CATEGORY_SELECT:
                              undo_commit(&undo, &world.players, &world.tilemap, &world.blocks, &world.interactives);
                              coord_clear(mouse_select_world(mouse_screen, camera), &world.tilemap, &world.interactives,
                                          world.interactive_qt, &world.blocks);
                              break;
                         case EDITOR_MODE_STAMP_SELECT:
                         case EDITOR_MODE_STAMP_HIDE:
                         {
                              undo_commit(&undo, &world.players, &world.tilemap, &world.blocks, &world.interactives);
                              Coord_t start = mouse_select_world(mouse_screen, camera);
                              Coord_t end = start + stamp_array_dimensions(editor.category_array.elements[editor.category].elements + editor.stamp);
                              for(S16 j = start.y; j < end.y; j++){
                                   for(S16 i = start.x; i < end.x; i++){
                                        Coord_t coord {i, j};
                                        coord_clear(coord, &world.tilemap, &world.interactives, world.interactive_qt, &world.blocks);
                                   }
                              }
                         } break;
                         case EDITOR_MODE_SELECTION_MANIPULATION:
                         {
                              undo_commit(&undo, &world.players, &world.tilemap, &world.blocks, &world.interactives);
                              Rect_t selection_bounds = editor_selection_bounds(&editor);
                              for(S16 j = selection_bounds.bottom; j <= selection_bounds.top; j++){
                                   for(S16 i = selection_bounds.left; i <= selection_bounds.right; i++){
                                        Coord_t coord {i, j};
                                        coord_clear(coord, &world.tilemap, &world.interactives, world.interactive_qt, &world.blocks);
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
                         seeked_with_mouse = false;

                         switch(editor.mode){
                         default:
                              break;
                         case EDITOR_MODE_CREATE_SELECTION:
                         {
                              editor.selection_end = mouse_select_world(mouse_screen, camera);

                              sort_selection(&editor);

                              destroy(&editor.selection);

                              S16 stamp_count = (S16)((((editor.selection_end.x - editor.selection_start.x) + 1) *
                                                       ((editor.selection_end.y - editor.selection_start.y) + 1)) * 2);
                              init(&editor.selection, stamp_count);
                              S16 stamp_index = 0;
                              for(S16 j = editor.selection_start.y; j <= editor.selection_end.y; j++){
                                   for(S16 i = editor.selection_start.x; i <= editor.selection_end.x; i++){
                                        Coord_t coord = {i, j};
                                        Coord_t offset = coord - editor.selection_start;

                                        // tile id
                                        Tile_t* tile = tilemap_get_tile(&world.tilemap, coord);
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
                                        auto* interactive = quad_tree_interactive_find_at(world.interactive_qt, coord);
                                        if(interactive){
                                             resize(&editor.selection, editor.selection.count + (S16)(1));
                                             auto* stamp = editor.selection.elements + (editor.selection.count - 1);
                                             stamp->type = STAMP_TYPE_INTERACTIVE;
                                             stamp->interactive = *interactive;
                                             stamp->offset = offset;
                                        }

                                        for(S16 b = 0; b < world.blocks.count; b++){
                                             auto* block = world.blocks.elements + b;
                                             if(pos_to_coord(block->pos) == coord){
                                                  resize(&editor.selection, editor.selection.count + (S16)(1));
                                                  auto* stamp = editor.selection.elements + (editor.selection.count - 1);
                                                  stamp->type = STAMP_TYPE_BLOCK;
                                                  stamp->block.rotation = block->rotation;
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

                    if(seeked_with_mouse && demo.mode == DEMO_MODE_PLAY){
                         demo.seek_frame = (S64)((F32)(demo.last_frame) * mouse_screen.x);

                         if(demo.seek_frame < frame_count){
                              // TODO: compress with same comment elsewhere in this file
                              fetch_cache_for_demo_seek(&world, &demo_starting_tilemap, &demo_starting_blocks, &demo_starting_interactives);

                              reset_map(player_start, &world, &undo);

                              // reset some vars
                              player_action = {};

                              demo.entry_index = 0;
                              frame_count = 0;
                         }else if(demo.seek_frame == frame_count){
                              demo.seek_frame = -1;
                         }
                    }
                    break;
               }
          }

          if(!demo.paused || demo.seek_frame >= 0){
               reset_tilemap_light(&world);

               // update interactives
               for(S16 i = 0; i < world.interactives.count; i++){
                    Interactive_t* interactive = world.interactives.elements + i;
                    if(interactive->type == INTERACTIVE_TYPE_POPUP){
                         lift_update(&interactive->popup.lift, POPUP_TICK_DELAY, dt, 1, POPUP_MAX_LIFT_TICKS);

                         for(S16 p = 0; p < world.players.count; p++){
                              auto* player = world.players.elements + p;
                              Coord_t player_coord = pos_to_coord(player->pos);
                              if(interactive->coord == player_coord && interactive->popup.lift.ticks == player->pos.z + 2){
                                  player->pos.z++;

                                  // if you are getting pushed up, it's hard to keep your grip!
                                  player->push_time = 0.0f;
                              }
                         }
                    }else if(interactive->type == INTERACTIVE_TYPE_DOOR){
                         lift_update(&interactive->door.lift, POPUP_TICK_DELAY, dt, 0, DOOR_MAX_HEIGHT);
                    }else if(interactive->type == INTERACTIVE_TYPE_PRESSURE_PLATE){
                         bool should_be_down = false;
                         for(S16 p = 0; p < world.players.count; p++){
                              if(world.players.elements[p].pos.z != 0) continue;
                              Coord_t player_coord = pos_to_coord(world.players.elements[p].pos);
                              if(interactive->coord == player_coord){
                                   should_be_down = true;
                                   break;
                              }
                         }

                         if(!should_be_down){
                              Tile_t* tile = tilemap_get_tile(&world.tilemap, interactive->coord);
                              if(tile){
                                   if(!tile_is_iced(tile)){
                                        Rect_t rect = rect_to_check_surrounding_blocks(coord_to_pixel_at_center(interactive->coord));

                                        S16 block_count = 0;
                                        Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
                                        quad_tree_find_in(world.block_qt, rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

                                        for(S16 b = 0; b < block_count; b++){
                                             if(blocks[b]->pos.z != 0) continue;

                                             Coord_t bottom_left = pixel_to_coord(blocks[b]->pos.pixel);
                                             Coord_t bottom_right = pixel_to_coord(block_bottom_right_pixel(blocks[b]->pos.pixel));
                                             Coord_t top_left = pixel_to_coord(block_top_left_pixel(blocks[b]->pos.pixel));
                                             Coord_t top_right = pixel_to_coord(block_top_right_pixel(blocks[b]->pos.pixel));
                                             if(interactive->coord == bottom_left ||
                                                interactive->coord == bottom_right ||
                                                interactive->coord == top_left ||
                                                interactive->coord == top_right){
                                                  should_be_down = true;
                                                  break;
                                             }
                                        }
                                   }
                              }
                         }

                         if(should_be_down != interactive->pressure_plate.down){
                              activate(&world, interactive->coord);
                              interactive->pressure_plate.down = should_be_down;
                         }
                    }
               }

               // update arrows
               for(S16 i = 0; i < ARROW_ARRAY_MAX; i++){
                    Arrow_t* arrow = world.arrows.arrows + i;
                    if(!arrow->alive) continue;

                    Coord_t pre_move_coord = pixel_to_coord(arrow->pos.pixel);

                    if(arrow->element == ELEMENT_FIRE){
                         illuminate(pre_move_coord, 255 - LIGHT_DECAY, &world);
                    }

                    if(arrow->stuck_time > 0.0f){
                         arrow->stuck_time += dt;

                         switch(arrow->stuck_type){
                         default:
                              break;
                         case STUCK_BLOCK:
                              arrow->pos = arrow->stuck_block->pos + arrow->stuck_offset;
                              break;
                         }

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
                    Coord_t post_move_coord = pixel_to_coord(arrow->pos.pixel);

                    Rect_t coord_rect = rect_surrounding_coord(post_move_coord);

                    S16 block_count = 0;
                    Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
                    quad_tree_find_in(world.block_qt, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);
                    for(S16 b = 0; b < block_count; b++){
                         // blocks on the coordinate and on the ground block light
                         Rect_t block_rect = block_get_rect(blocks[b]);
                         S16 block_index = (S16)(blocks[b] - world.blocks.elements);
                         S8 block_bottom = blocks[b]->pos.z;
                         S8 block_top = block_bottom + HEIGHT_INTERVAL;
                         if(pixel_in_rect(arrow->pos.pixel, block_rect) && arrow->element_from_block != block_index){
                              if(arrow->pos.z >= block_bottom && arrow->pos.z <= block_top){
                                   arrow->stuck_time = dt;
                                   arrow->stuck_offset = arrow->pos - blocks[b]->pos;
                                   arrow->stuck_type = STUCK_BLOCK;
                                   arrow->stuck_block = blocks[b];
                              }else if(arrow->pos.z > block_top && arrow->pos.z < (block_top + HEIGHT_INTERVAL)){
                                   arrow->element_from_block = block_index;
                                   if(arrow->element != blocks[b]->element){
                                        Element_t arrow_element = arrow->element;
                                        arrow->element = transition_element(arrow->element, blocks[b]->element);
                                        if(arrow_element){
                                             blocks[b]->element = transition_element(blocks[b]->element, arrow_element);
                                             if(blocks[b]->entangle_index >= 0 && blocks[b]->entangle_index < world.blocks.count){
                                                  S16 original_index = blocks[b] - world.blocks.elements;
                                                  S16 entangle_index = blocks[b]->entangle_index;
                                                  while(entangle_index != original_index && entangle_index >= 0){
                                                       Block_t* entangled_block = world.blocks.elements + entangle_index;
                                                       entangled_block->element = transition_element(entangled_block->element, arrow_element);
                                                       entangle_index = entangled_block->entangle_index;
                                                  }
                                             }
                                        }
                                   }
                              }
                              break;
                         }
                    }

                    if(block_count == 0){
                         arrow->element_from_block = -1;
                    }

                    Coord_t skip_coord[DIRECTION_COUNT];
                    find_portal_adjacents_to_skip_collision_check(pre_move_coord, world.interactive_qt, skip_coord);

                    if(pre_move_coord != post_move_coord){
                         bool skip = false;
                         for (auto d : skip_coord) {
                              if(post_move_coord == d) skip = true;
                         }

                         if(!skip){
                              Tile_t* tile = tilemap_get_tile(&world.tilemap, post_move_coord);
                              if(tile_is_solid(tile)){
                                   arrow->stuck_time = dt;
                              }
                         }

                         // catch or give elements
                         if(arrow->element == ELEMENT_FIRE){
                              melt_ice(post_move_coord, 0, &world);
                         }else if(arrow->element == ELEMENT_ICE){
                              spread_ice(post_move_coord, 0, &world);
                         }

                         Interactive_t* interactive = quad_tree_interactive_find_at(world.interactive_qt, post_move_coord);
                         if(interactive){
                              switch(interactive->type){
                              default:
                                   break;
                              case INTERACTIVE_TYPE_LEVER:
                                   if(arrow->pos.z >= HEIGHT_INTERVAL){
                                        activate(&world, post_move_coord);
                                   }else{
                                        arrow->stuck_time = dt;
                                   }
                                   break;
                              case INTERACTIVE_TYPE_DOOR:
                                   if(interactive->door.lift.ticks < arrow->pos.z){
                                        arrow->stuck_time = dt;
                                        // TODO: stuck in door
                                   }
                                   break;
                              case INTERACTIVE_TYPE_POPUP:
                                   if(interactive->popup.lift.ticks > arrow->pos.z){
                                        LOG("arrow z: %d, popup lift: %d\n", arrow->pos.z, interactive->popup.lift.ticks);
                                        arrow->stuck_time = dt;
                                        // TODO: stuck in popup
                                   }
                                   break;
                              case INTERACTIVE_TYPE_PORTAL:
                                   if(!interactive->portal.on){
                                        arrow->stuck_time = dt;
                                        // TODO: arrow drops if portal turns on
                                   }else if(!portal_has_destination(post_move_coord, &world.tilemap, world.interactive_qt)){
                                        // TODO: arrow drops if portal turns on
                                        arrow->stuck_time = dt;
                                   }
                                   break;
                              }
                         }

                         auto teleport_result = teleport_position_across_portal(arrow->pos, Vec_t{}, &world,
                                                                                pre_move_coord, post_move_coord);
                         if(teleport_result.count > 0){
                              arrow->pos = teleport_result.results[0].pos;
                              arrow->face = direction_rotate_clockwise(arrow->face, teleport_result.results[0].rotations);

                              // TODO: entangle
                         }
                    }
               }

               // TODO: deal with all this for multiple players
               bool rotated_move_actions[DIRECTION_COUNT];
               bool user_stopping_x = false;
               bool user_stopping_y = false;

               // update player
               for(S16 i = 0; i < world.players.count; i++){
                    Player_t* player = world.players.elements + i;

                    memset(rotated_move_actions, 0, sizeof(rotated_move_actions));

                    for(int d = 0; d < 4; d++){
                         if(player_action.move[d]){
                              Direction_t rot_dir = direction_rotate_clockwise((Direction_t)(d), player->move_rotation[d]);
                              rot_dir = direction_rotate_clockwise(rot_dir, player->rotation);
                              rotated_move_actions[rot_dir] = true;
                              if(player->reface) player->face = static_cast<Direction_t>(rot_dir);
                         }
                    }

                    player->accel = vec_zero();

                    if(rotated_move_actions[DIRECTION_RIGHT]){
                         if(rotated_move_actions[DIRECTION_LEFT]){
                              user_stopping_x = true;

                              if(player->vel.x > 0){
                                   player->accel.x -= PLAYER_ACCEL;
                              }else if(player->vel.x < 0){
                                   player->accel.x += PLAYER_ACCEL;
                              }
                         }else{
                              player->accel.x += PLAYER_ACCEL;
                         }
                    }else if(rotated_move_actions[DIRECTION_LEFT]){
                         player->accel.x -= PLAYER_ACCEL;
                    }else if(player->vel.x > 0){
                         user_stopping_x = true;
                         player->accel.x -= PLAYER_ACCEL;
                    }else if(player->vel.x < 0){
                         user_stopping_x = true;
                         player->accel.x += PLAYER_ACCEL;
                    }

                    if(rotated_move_actions[DIRECTION_UP]){
                         if(rotated_move_actions[DIRECTION_DOWN]){
                              user_stopping_y = true;

                              if(player->vel.y > 0){
                                   player->accel.y -= PLAYER_ACCEL;
                              }else if(player->vel.y < 0){
                                   player->accel.y += PLAYER_ACCEL;
                              }
                         }else{
                              player->accel.y += PLAYER_ACCEL;
                         }
                    }else if(rotated_move_actions[DIRECTION_DOWN]){
                         player->accel.y -= PLAYER_ACCEL;
                    }else if(player->vel.y > 0){
                         user_stopping_y = true;
                         player->accel.y -= PLAYER_ACCEL;
                    }else if(player->vel.y < 0){
                         user_stopping_y = true;
                         player->accel.y += PLAYER_ACCEL;
                    }

                    if(player_action.activate && !player_action.last_activate){
                         undo_commit(&undo, &world.players, &world.tilemap, &world.blocks, &world.interactives);
                         activate(&world, pos_to_coord(player->pos) + player->face);
                    }

                    if(player_action.undo){
                         undo_commit(&undo, &world.players, &world.tilemap, &world.blocks, &world.interactives, true);
                         undo_revert(&undo, &world.players, &world.tilemap, &world.blocks, &world.interactives);
                         quad_tree_free(world.interactive_qt);
                         world.interactive_qt = quad_tree_build(&world.interactives);
                         quad_tree_free(world.block_qt);
                         world.block_qt = quad_tree_build(&world.blocks);
                         player_action.undo = false;
                    }

                    if(player->has_bow && player_action.shoot && player->bow_draw_time < PLAYER_BOW_DRAW_DELAY){
                         player->bow_draw_time += dt;
                    }else if(!player_action.shoot){
                         if(player->bow_draw_time >= PLAYER_BOW_DRAW_DELAY){
                              undo_commit(&undo, &world.players, &world.tilemap, &world.blocks, &world.interactives);
                              Position_t arrow_pos = player->pos;
                              switch(player->face){
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
                              arrow_spawn(&world.arrows, arrow_pos, player->face);
                         }
                         player->bow_draw_time = 0.0f;
                    }

                    if(!player_action.move[DIRECTION_LEFT] && !player_action.move[DIRECTION_RIGHT] &&
                       !player_action.move[DIRECTION_UP] && !player_action.move[DIRECTION_DOWN]){
                         player->walk_frame = 1;
                    }else{
                         player->walk_frame_time += dt;

                         if(player->walk_frame_time > PLAYER_WALK_DELAY){
                              if(vec_magnitude(player->vel) > PLAYER_IDLE_SPEED){
                                   player->walk_frame_time = 0.0f;

                                   player->walk_frame += player->walk_frame_delta;
                                   if(player->walk_frame > 2 || player->walk_frame < 0){
                                        player->walk_frame = 1;
                                        player->walk_frame_delta = -player->walk_frame_delta;
                                   }
                              }else{
                                   player->walk_frame = 1;
                                   player->walk_frame_time = 0.0f;
                              }
                         }
                    }

                    bool held_up = false;

                    auto player_coord = pos_to_coord(player->pos);
                    Interactive_t* interactive = quad_tree_interactive_find_at(world.interactive_qt, player_coord);
                    if(interactive && interactive->type == INTERACTIVE_TYPE_POPUP &&
                       interactive->popup.lift.ticks == player->pos.z + 1){
                        held_up = true;
                    }

                    Rect_t coord_rect = rect_surrounding_coord(player_coord);

                    S16 block_count = 0;
                    Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];

                    quad_tree_find_in(world.block_qt, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);
                    for(S16 b = 0; b < block_count; b++){
                        auto block_rect = block_get_rect(blocks[b]);
                        if(pixel_in_rect(player->pos.pixel, block_rect) && blocks[b]->pos.z == player->pos.z - HEIGHT_INTERVAL){
                            held_up = true;
                            break;
                        }
                    }

                    if(!held_up && player->pos.z > 0){
                        player->pos.z--;
                    }
               }

               Position_t room_center = coord_to_pos(Coord_t{8, 8});
               Position_t camera_movement = room_center - camera;
               camera += camera_movement * 0.05f;

               for(S16 i = 0; i < world.players.count; i++){
                    Player_t* player = world.players.elements + i;

                    auto pushing_block_dir = direction_rotate_clockwise(player->pushing_block_dir, player->pushing_block_rotation);
                    if(player->pushing_block >= 0 && player->face == pushing_block_dir){
                         player->prev_pushing_block = player->pushing_block;
                    }else{
                         player->prev_pushing_block = -1;
                    }
                    player->pushing_block = -1;

                    player->teleport_pushing_block = -1;

                    player->prev_vel = player->vel;

                    player->pos_delta.x = calc_position_motion(player->vel.x, player->accel.x, dt);
                    player->vel.x = calc_velocity_motion(player->vel.x, player->accel.x, dt);

                    player->pos_delta.y = calc_position_motion(player->vel.y, player->accel.y, dt);
                    player->vel.y = calc_velocity_motion(player->vel.y, player->accel.y, dt);

                    // handle free form stopping
                    if(user_stopping_x){
                         if((player->prev_vel.x > 0 && player->vel.x < 0) ||
                            (player->prev_vel.x < 0 && player->vel.x > 0)){
                              float dt_consumed = -player->prev_vel.x / player->accel.x;

                              player->pos_delta.x = player->prev_vel.x * dt_consumed + 0.5f * player->accel.x * dt_consumed * dt_consumed;

                              player->accel.x = 0;
                              player->vel.x = 0;
                              player->prev_vel.x = 0;
                         }
                    }

                    if(user_stopping_y){
                         if((player->prev_vel.y > 0 && player->vel.y < 0) ||
                            (player->prev_vel.y < 0 && player->vel.y > 0)){
                              float dt_consumed = -player->prev_vel.y / player->accel.y;

                              player->pos_delta.y = player->prev_vel.y * dt_consumed + 0.5f * player->accel.y * dt_consumed * dt_consumed;

                              player->accel.y = 0;
                              player->vel.y = 0;
                              player->prev_vel.y = 0;
                         }
                    }

                    if(fabs(player->vel.x) > PLAYER_MAX_VEL){
                         // vf = v0 + a * dt
                         // (vf - v0) / a = dt
                         float max_vel_mag = PLAYER_MAX_VEL;
                         if(player->vel.x < 0) max_vel_mag = -max_vel_mag;
                         float dt_consumed = (max_vel_mag - player->prev_vel.x) / player->accel.x;
                         float dt_leftover = dt - dt_consumed;

                         player->pos_delta.x = player->prev_vel.x * dt + (0.5f * player->accel.x * dt * dt);
                         player->vel.x = (player->vel.x > 0) ? PLAYER_MAX_VEL : -PLAYER_MAX_VEL;

                         player->pos_delta.x += player->vel.x * dt_leftover;
                    }

                    if(fabs(player->vel.y) > PLAYER_MAX_VEL){
                         float max_vel_mag = PLAYER_MAX_VEL;
                         if(player->vel.y < 0) max_vel_mag = -max_vel_mag;
                         float dt_consumed = (max_vel_mag - player->prev_vel.y) / player->accel.y;
                         float dt_leftover = dt - dt_consumed;

                         player->pos_delta.y = player->prev_vel.y * dt + (0.5f * player->accel.y * dt * dt);
                         player->vel.y = (player->vel.y > 0) ? PLAYER_MAX_VEL : -PLAYER_MAX_VEL;

                         player->pos_delta.y += player->vel.y * dt_leftover;
                    }

                    // limit the max velocity and normalize the vector
                    float max_pos_delta = PLAYER_MAX_VEL * dt + 0.5f * PLAYER_ACCEL * dt * dt;
                    if(vec_magnitude(player->pos_delta) > max_pos_delta){
                         player->pos_delta = vec_normalize(player->pos_delta) * max_pos_delta;
                    }

                    Coord_t player_previous_coord = pos_to_coord(player->pos);
                    Coord_t player_coord = pos_to_coord(player->pos + player->pos_delta);

                    // teleport position
                    auto teleport_result = teleport_position_across_portal(player->pos, player->pos_delta, &world,
                                                                           player_previous_coord, player_coord);
                    auto teleport_clone_id = player->clone_id;
                    if(player_coord != player->clone_start){
                         // if we are going back to our clone portal, then all clones should go back

                         // find the index closest to our original clone portal
                         F32 shortest_distance = FLT_MAX;
                         auto clone_start_center = coord_to_pixel_at_center(player->clone_start);

                         for(S8 t = 0; t < teleport_result.count; t++){
                              F32 distance = pixel_distance_between(clone_start_center, teleport_result.results[t].pos.pixel);
                              if(distance < shortest_distance){
                                   shortest_distance = distance;
                                   teleport_clone_id = t;
                              }
                         }
                    }

                    // if a teleport happened, update the position
                    if(teleport_result.count){
                         assert(teleport_result.count > teleport_clone_id);

                         player->teleport = true;
                         player->teleport_pos = teleport_result.results[teleport_clone_id].pos;
                         player->teleport_pos_delta = teleport_result.results[teleport_clone_id].delta;
                         player->teleport_rotation = teleport_result.results[teleport_clone_id].rotations;
                         player->teleport_face = direction_rotate_clockwise(player->face, teleport_result.results[teleport_clone_id].rotations);
                    }else{
                         // if we collide enough to reduce our pos_delta, we could have previously teleported and are no
                         // longer teleporting in the same frame.
                         player->teleport = false;
                    }

                    if(player->stopping_block_from_time > 0){
                         player->stopping_block_from_time -= dt;
                         if(player->stopping_block_from_time < 0){
                              player->stopping_block_from_time = 0;
                              player->stopping_block_from = DIRECTION_COUNT;
                         }
                    }else{
                         player->stopping_block_from = DIRECTION_COUNT;
                    }
               }

               // block movement
               // do a pass moving the block as far as possible, so that collision doesn't rely on order of blocks in the array
               for(S16 i = 0; i < world.blocks.count; i++){
                    Block_t* block = world.blocks.elements + i;

                    block->prev_push_mask = block->cur_push_mask;
                    block->cur_push_mask = DIRECTION_MASK_NONE;

                    block->prev_vel = block->vel;

                    block->accel.x = calc_accel_component_move(block->horizontal_move, block->accel_magnitudes.x);
                    block->accel.y = calc_accel_component_move(block->vertical_move, block->accel_magnitudes.y);

                    block->pos_delta.x = calc_position_motion(block->vel.x, block->accel.x, dt);
                    block->vel.x = calc_velocity_motion(block->vel.x, block->accel.x, dt);

                    block->pos_delta.y = calc_position_motion(block->vel.y, block->accel.y, dt);
                    block->vel.y = calc_velocity_motion(block->vel.y, block->accel.y, dt);

                    // teleport if necessary
                    auto block_center = block_get_center(block);
                    auto premove_coord = block_get_coord(block);
                    auto coord = block_get_coord(block->pos + block->pos_delta);
                    auto teleport_result = teleport_position_across_portal(block_center, block->pos_delta, &world, premove_coord, coord);
                    if(teleport_result.count > block->clone_id){
                         block->teleport = true;
                         block->teleport_pos = teleport_result.results[block->clone_id].pos;
                         block->teleport_pos.pixel -= HALF_TILE_SIZE_PIXEL;

                         block->teleport_pos_delta = teleport_result.results[block->clone_id].delta;
                         block->teleport_vel = vec_rotate_quadrants_clockwise(block->vel, teleport_result.results[block->clone_id].rotations);
                         block->teleport_accel = vec_rotate_quadrants_clockwise(block->accel, teleport_result.results[block->clone_id].rotations);
                         block->teleport_rotation = teleport_result.results[block->clone_id].rotations;

                         if(block->teleport_rotation % 2){
                              Move_t tmp = block->horizontal_move;
                              block->horizontal_move = block->vertical_move;
                              block->vertical_move = tmp;

                              for(S8 r = 0; r < block->teleport_rotation; r++){
                                   block->prev_push_mask = direction_mask_rotate_clockwise(block->prev_push_mask);
                              }
                         }

                         block->teleport_horizontal_move = block->horizontal_move;
                         block->teleport_vertical_move = block->vertical_move;

                    }else{
                         block->teleport = false;
                    }

                    block->coast_horizontal = BLOCK_COAST_NONE;
                    block->coast_vertical = BLOCK_COAST_NONE;
               }

               // do multiple passes here so that entangled blocks know for sure if their entangled counterparts are coasting and index order doesn't matter
               for(S16 j = 0; j < 2; j++){
                    for(S16 i = 0; i < world.blocks.count; i++){
                         Block_t* block = world.blocks.elements + i;

                         if(block->teleport && block_on_ice(block->teleport_pos, block->teleport_pos_delta, &world.tilemap, world.interactive_qt)){
                              block->coast_horizontal = BLOCK_COAST_ICE;
                              block->coast_vertical = BLOCK_COAST_ICE;
                         }else if(block_on_ice(block->pos, block->pos_delta, &world.tilemap, world.interactive_qt)){
                              block->coast_horizontal = BLOCK_COAST_ICE;
                              block->coast_vertical = BLOCK_COAST_ICE;
                         }else{
                              if(block->horizontal_move.state == MOVE_STATE_STARTING ||
                                 block->horizontal_move.state == MOVE_STATE_COASTING){
                                   if(block->horizontal_move.sign == MOVE_SIGN_POSITIVE){
                                        if(block->prev_push_mask & DIRECTION_MASK_RIGHT){
                                             block->coast_horizontal = BLOCK_COAST_PLAYER;
                                        }
                                   }else if(block->horizontal_move.sign == MOVE_SIGN_NEGATIVE){
                                        if(block->prev_push_mask & DIRECTION_MASK_LEFT){
                                             block->coast_horizontal = BLOCK_COAST_PLAYER;
                                        }
                                   }
                              }

                              if(block->vertical_move.state == MOVE_STATE_STARTING ||
                                 block->vertical_move.state == MOVE_STATE_COASTING){
                                   if(block->vertical_move.sign == MOVE_SIGN_POSITIVE){
                                        if(block->prev_push_mask & DIRECTION_MASK_UP){
                                             block->coast_vertical = BLOCK_COAST_PLAYER;
                                        }
                                   }else if(block->vertical_move.sign == MOVE_SIGN_NEGATIVE){
                                        if(block->prev_push_mask & DIRECTION_MASK_DOWN){
                                             block->coast_vertical = BLOCK_COAST_PLAYER;
                                        }
                                   }
                              }
                         }

                         if(block->coast_vertical <= BLOCK_COAST_ICE  || block->coast_horizontal <= BLOCK_COAST_ICE){
                              // TODO: this logic isn't strictly correct when the block teleports, but I also don't think there is any harm right now?
                              for(S16 p = 0; p < world.players.count; p++){
                                   Player_t* player = world.players.elements + p;

                                   // is the player pushing us ?
                                   if(player->prev_pushing_block < 0) continue;

                                   Block_t* player_prev_pushing_block = world.blocks.elements + player->prev_pushing_block;
                                   if(player_prev_pushing_block == block){
                                        switch(player->face){
                                        default:
                                             break;
                                        case DIRECTION_LEFT:
                                        case DIRECTION_RIGHT:
                                        {
                                             // only coast the block is actually moving
                                             Vec_t block_horizontal_vel = {block->vel.x, 0};
                                             if(player->face == vec_direction(block_horizontal_vel)){
                                                  block->coast_horizontal = BLOCK_COAST_PLAYER;
                                             }
                                             break;
                                        }
                                        case DIRECTION_UP:
                                        case DIRECTION_DOWN:
                                        {
                                             Vec_t block_vertical_vel = {0, block->vel.y};
                                             if(player->face == vec_direction(block_vertical_vel)){
                                                  block->coast_vertical = BLOCK_COAST_PLAYER;
                                             }
                                             break;
                                        }
                                        }
                                   }else if(blocks_are_entangled(block, player_prev_pushing_block, &world.blocks)){
                                        Block_t* entangled_block = player_prev_pushing_block;

                                        auto rotations_between = blocks_rotations_between(block, entangled_block);

                                        if(entangled_block->coast_horizontal > BLOCK_COAST_NONE){
                                             if(rotations_between % 2 == 0){
                                                  block->coast_horizontal = entangled_block->coast_horizontal;

                                                  // TODO: compress this code with the 3 instances below it
                                                  if(block->horizontal_move.state == MOVE_STATE_IDLING && player->push_time > BLOCK_PUSH_TIME){
                                                       Vec_t block_horizontal_vel = {entangled_block->vel.x, 0};
                                                       auto block_move_dir = vec_direction(block_horizontal_vel);
                                                       if(block_move_dir != DIRECTION_COUNT){
                                                            auto direction_to_push = direction_rotate_clockwise(block_move_dir, rotations_between);
                                                            block_push(block, direction_to_push, &world, false);
                                                       }
                                                  }
                                             }else{
                                                  block->coast_vertical = entangled_block->coast_horizontal;

                                                  if(block->vertical_move.state == MOVE_STATE_IDLING && player->push_time > BLOCK_PUSH_TIME){
                                                       Vec_t block_horizontal_vel = {entangled_block->vel.x, 0};
                                                       auto block_move_dir = vec_direction(block_horizontal_vel);
                                                       if(block_move_dir != DIRECTION_COUNT){
                                                            auto direction_to_push = direction_rotate_clockwise(block_move_dir, rotations_between);
                                                            block_push(block, direction_to_push, &world, false);
                                                       }
                                                  }
                                             }
                                        }

                                        if(entangled_block->coast_vertical > BLOCK_COAST_NONE){
                                             if(rotations_between % 2 == 0){
                                                  block->coast_vertical = entangled_block->coast_vertical;

                                                  if(block->vertical_move.state == MOVE_STATE_IDLING && player->push_time > BLOCK_PUSH_TIME){
                                                       Vec_t block_vertical_vel = {0, entangled_block->vel.y};
                                                       auto block_move_dir = vec_direction(block_vertical_vel);
                                                       if(block_move_dir != DIRECTION_COUNT){
                                                            auto direction_to_push = direction_rotate_clockwise(block_move_dir, rotations_between);
                                                            block_push(block, direction_to_push, &world, false);
                                                       }
                                                  }
                                             }else{
                                                  block->coast_horizontal = entangled_block->coast_vertical;

                                                  if(block->horizontal_move.state == MOVE_STATE_IDLING && player->push_time > BLOCK_PUSH_TIME){
                                                       Vec_t block_vertical_vel = {0, entangled_block->vel.y};
                                                       auto block_move_dir = vec_direction(block_vertical_vel);
                                                       if(block_move_dir != DIRECTION_COUNT){
                                                            auto direction_to_push = direction_rotate_clockwise(block_move_dir, rotations_between);
                                                            block_push(block, direction_to_push, &world, false);
                                                       }
                                                  }
                                             }
                                        }
                                   }
                              }
                         }
                    }
               }

               for(S16 i = 0; i < world.blocks.count; i++){
                    Block_t* block = world.blocks.elements + i;

                    // TODO: creating this potentially big vector could lead to precision issues
                    Vec_t pos_vec = pos_to_vec(block->pos);

                    update_motion_grid_aligned(&block->horizontal_move, motion_x_component(block),
                                               block->coast_horizontal != BLOCK_COAST_NONE, dt, block->accel.x,
                                               BLOCK_ACCEL_DISTANCE, pos_vec.x);

                    update_motion_grid_aligned(&block->vertical_move, motion_y_component(block),
                                               block->coast_vertical != BLOCK_COAST_NONE, dt, block->accel.y,
                                               BLOCK_ACCEL_DISTANCE, pos_vec.y);
               }

               for(S16 i = 0; i < world.blocks.count; i++){
                    auto block = world.blocks.elements + i;

                    block->held_up = block_held_up_by_another_block(block, world.block_qt);

                    // TODO: should we care about the decimal component of the position ?
                    Coord_t rect_coords[4];
                    bool pushed_up = false;
                    get_rect_coords(block_get_rect(block), rect_coords);
                    for(S8 c = 0; c < 4; c++){
                         auto* interactive = quad_tree_interactive_find_at(world.interactive_qt, rect_coords[c]);
                         if(interactive){
                              if(interactive->type == INTERACTIVE_TYPE_POPUP){
                                   if(!pushed_up && (block->pos.z == interactive->popup.lift.ticks - 2)){
                                        Block_t* above_block = block_held_down_by_another_block(block, world.block_qt);
                                        while(above_block){
                                             Block_t* tmp_block = above_block;
                                             above_block = block_held_down_by_another_block(above_block, world.block_qt);
                                             tmp_block->pos.z++;
                                             tmp_block->held_up = true;
                                        }
                                        block->pos.z++;
                                        block->held_up = true;
                                        pushed_up = true;
                                   }else if(!block->held_up && block->pos.z == (interactive->popup.lift.ticks - 1)){
                                        block->held_up = true;
                                   }
                              }
                         }
                    }
               }

               for(S16 i = 0; i < world.blocks.count; i++){
                    auto block = world.blocks.elements + i;

                    if(!block->held_up && block->pos.z > 0){
                         block->fall_time += dt;
                         if(block->fall_time >= FALL_TIME){
                              block->fall_time -= FALL_TIME;
                              block->pos.z--;
                         }
                    }
               }


               // unbounded collision: this should be exciting
               // we have our initial position and our initial pos_delta, update pos_delta for all players and blocks until nothing is colliding anymore
               const int max_collision_attempts = 16;
               int collision_attempts = 0;
               bool collided = true;
               while(collided && collision_attempts < max_collision_attempts){
                    collided = false;

                    // do a collision pass on each block
                    S16 update_blocks_count = world.blocks.count;
                    for(S16 i = 0; i < update_blocks_count; i++){
                         Block_t* block = world.blocks.elements + i;

                         block->successfully_moved = true;

                         bool stop_on_boundary_x = false;
                         bool stop_on_boundary_y = false;

                         // if we are being stopped by the player and have moved more than the player radius (which is
                         // a check to ensure we don't stop a block instantaneously) then stop on the coordinate boundaries
                         if(block->stopped_by_player_horizontal &&
                            block->horizontal_move.distance > PLAYER_RADIUS){
                              stop_on_boundary_x = true;
                         }

                         if(block->stopped_by_player_vertical &&
                            block->vertical_move.distance > PLAYER_RADIUS){
                              stop_on_boundary_y = true;
                         }

                         if(block->pos_delta.x != 0.0f || block->pos_delta.y != 0.0f){
                              auto result = check_block_collision_with_other_blocks(block->pos,
                                                                                    block->pos_delta,
                                                                                    block->vel,
                                                                                    block->accel,
                                                                                    block->stop_on_pixel_x,
                                                                                    block->stop_on_pixel_y,
                                                                                    block->horizontal_move,
                                                                                    block->vertical_move,
                                                                                    i,
                                                                                    block->clone_start.x > 0,
                                                                                    &world);

                              if(block->teleport){
                                   auto teleport_result = check_block_collision_with_other_blocks(block->teleport_pos,
                                                                                                  block->teleport_pos_delta,
                                                                                                  block->teleport_vel,
                                                                                                  block->teleport_accel,
                                                                                                  block->stop_on_pixel_x,
                                                                                                  block->stop_on_pixel_y,
                                                                                                  block->horizontal_move,
                                                                                                  block->vertical_move,
                                                                                                  i,
                                                                                                  block->clone_start.x > 0,
                                                                                                  &world);
                                   // TODO: find the closer collision between the regular collided result and the teleported collided result
                                   if(teleport_result.collided){
                                        collided = true;

                                        block->successfully_moved = false;

                                        block->teleport_pos_delta = teleport_result.pos_delta;
                                        block->teleport_vel = teleport_result.vel;
                                        block->teleport_accel = teleport_result.accel;

                                        block->teleport_stop_on_pixel_x = teleport_result.stop_on_pixel_x;
                                        block->teleport_stop_on_pixel_y = teleport_result.stop_on_pixel_y;

                                        block->teleport_horizontal_move = teleport_result.horizontal_move;
                                        block->teleport_vertical_move = teleport_result.vertical_move;
                                   }
                              }

                              if(result.collided){
                                   collided = true;

                                   block->successfully_moved = false;

                                   if(result.collided_block_index >= 0 && blocks_are_entangled(result.collided_block_index, i, &world.blocks)){
                                        // TODO: I don't love indexing the blocks without checking the index is valid first
                                        auto* entangled_block = world.blocks.elements + result.collided_block_index;

                                        // the entangled block pos might be faked due to portals, so use the resulting collision pos instead of the actual position
                                        auto entangled_block_pos = result.collided_pos;

                                        // the result collided position is the center of the block, so handle this
                                        entangled_block_pos.pixel -= HALF_TILE_SIZE_PIXEL;

                                        auto final_block_pos = block->pos + block->pos_delta;
                                        auto pos_diff = pos_to_vec(final_block_pos - entangled_block_pos);

                                        U8 total_rotations = (block->rotation + entangled_block->rotation + result.collided_portal_rotations) % (S8)(DIRECTION_COUNT);

                                        // TODO: this 0.0001f is a hack, it used to be an equality check, but the
                                        //       numbers were slightly off in the case of rotated portals but not rotated entangled blocks
                                        auto pos_dimension_delta = fabs(fabs(pos_diff.x) - fabs(pos_diff.y));

                                        // if positions are diagonal to each other and the rotation between them is odd, check if we are moving into each other
                                        if(pos_dimension_delta < 0.0001f && (total_rotations) % 2 == 1){
                                             // TODO: switch to using block_inside_another_block() because that is actually what we care about
                                             auto entangle_result = check_block_collision_with_other_blocks(entangled_block->pos,
                                                                                                            entangled_block->pos_delta,
                                                                                                            entangled_block->vel,
                                                                                                            entangled_block->accel,
                                                                                                            entangled_block->stop_on_pixel_x,
                                                                                                            entangled_block->stop_on_pixel_y,
                                                                                                            entangled_block->horizontal_move,
                                                                                                            entangled_block->vertical_move,
                                                                                                            block->entangle_index,
                                                                                                            entangled_block->clone_start.x > 0,
                                                                                                            &world);
                                             if(entangle_result.collided){
                                                  // stop the blocks moving toward each other
                                                  static const VecMaskCollisionEntry_t table[] = {
                                                       {static_cast<S8>(DIRECTION_MASK_RIGHT | DIRECTION_MASK_UP), DIRECTION_LEFT, DIRECTION_UP, DIRECTION_DOWN, DIRECTION_RIGHT},
                                                       {static_cast<S8>(DIRECTION_MASK_RIGHT | DIRECTION_MASK_DOWN), DIRECTION_LEFT, DIRECTION_DOWN, DIRECTION_UP, DIRECTION_RIGHT},
                                                       {static_cast<S8>(DIRECTION_MASK_LEFT  | DIRECTION_MASK_UP), DIRECTION_LEFT, DIRECTION_DOWN, DIRECTION_UP, DIRECTION_RIGHT},
                                                       {static_cast<S8>(DIRECTION_MASK_LEFT  | DIRECTION_MASK_DOWN), DIRECTION_LEFT, DIRECTION_UP, DIRECTION_DOWN, DIRECTION_RIGHT},
                                                       // TODO: single direction mask things
                                                  };

                                                  // TODO: figure out if we are colliding through a portal or not

                                                  auto delta_vec = pos_to_vec(block->pos - entangled_block_pos);
                                                  auto delta_mask = vec_direction_mask(delta_vec);
                                                  auto move_mask = vec_direction_mask(block->pos_delta);
                                                  auto entangle_move_mask = vec_direction_mask(vec_rotate_quadrants_counter_clockwise(entangled_block->pos_delta, result.collided_portal_rotations));

                                                  Direction_t move_dir_to_stop = DIRECTION_COUNT;
                                                  Direction_t entangled_move_dir_to_stop = DIRECTION_COUNT;

                                                  for(S8 t = 0; t < (S8)(sizeof(table) / sizeof(table[0])); t++){
                                                       if(table[t].mask == delta_mask){
                                                            if(direction_in_mask(move_mask, table[t].move_a_1) &&
                                                               direction_in_mask(entangle_move_mask, table[t].move_b_1)){
                                                                 move_dir_to_stop = table[t].move_a_1;
                                                                 entangled_move_dir_to_stop = table[t].move_b_1;
                                                                 break;
                                                            }else if(direction_in_mask(move_mask, table[t].move_b_1) &&
                                                                     direction_in_mask(entangle_move_mask, table[t].move_a_1)){
                                                                 move_dir_to_stop = table[t].move_b_1;
                                                                 entangled_move_dir_to_stop = table[t].move_a_1;
                                                                 break;
                                                            }else if(direction_in_mask(move_mask, table[t].move_a_2) &&
                                                                     direction_in_mask(entangle_move_mask, table[t].move_b_2)){
                                                                 move_dir_to_stop = table[t].move_a_2;
                                                                 entangled_move_dir_to_stop = table[t].move_b_2;
                                                                 break;
                                                            }else if(direction_in_mask(move_mask, table[t].move_b_2) &&
                                                                     direction_in_mask(entangle_move_mask, table[t].move_a_2)){
                                                                 move_dir_to_stop = table[t].move_b_2;
                                                                 entangled_move_dir_to_stop = table[t].move_a_2;
                                                                 break;
                                                            }
                                                       }
                                                  }

                                                  if(move_dir_to_stop == DIRECTION_COUNT){
                                                       copy_block_collision_results(block, &result);
                                                  }else{
                                                       if(block_on_ice(block->pos, block->pos_delta, &world.tilemap, world.interactive_qt) &&
                                                          block_on_ice(entangled_block->pos, entangled_block->pos_delta, &world.tilemap, world.interactive_qt)){
                                                            // TODO: handle this case for blocks not entangled on ice
                                                            // handle pushing blocks diagonally

                                                            F32 block_instant_vel = 0;
                                                            F32 entangled_block_instant_vel = 0;

                                                            switch(move_dir_to_stop){
                                                            default:
                                                                 break;
                                                            case DIRECTION_LEFT:
                                                            case DIRECTION_RIGHT:
                                                                 block_instant_vel = block->vel.x;
                                                                 break;
                                                            case DIRECTION_UP:
                                                            case DIRECTION_DOWN:
                                                                 block_instant_vel = block->vel.y;
                                                                 break;
                                                            }

                                                            switch(entangled_move_dir_to_stop){
                                                            default:
                                                                 break;
                                                            case DIRECTION_LEFT:
                                                            case DIRECTION_RIGHT:
                                                                 entangled_block_instant_vel = entangled_block->vel.x;
                                                                 break;
                                                            case DIRECTION_UP:
                                                            case DIRECTION_DOWN:
                                                                 entangled_block_instant_vel = entangled_block->vel.y;
                                                                 break;
                                                            }

                                                            if(block_push(block, entangled_move_dir_to_stop, &world, true, entangled_block_instant_vel)){
                                                                 switch(entangled_move_dir_to_stop){
                                                                 default:
                                                                      break;
                                                                 case DIRECTION_LEFT:
                                                                 case DIRECTION_RIGHT:
                                                                      block->pos_delta.x = entangled_block_instant_vel * dt;
                                                                      block->pos.decimal.x = block->pos.decimal.y;
                                                                      break;
                                                                 case DIRECTION_UP:
                                                                 case DIRECTION_DOWN:
                                                                      block->pos_delta.y = entangled_block_instant_vel * dt;
                                                                      block->pos.decimal.y = block->pos.decimal.x;
                                                                      break;
                                                                 }
                                                            }
                                                            if(block_push(entangled_block, move_dir_to_stop, &world, true, block_instant_vel)){
                                                                 switch(move_dir_to_stop){
                                                                 default:
                                                                      break;
                                                                 case DIRECTION_LEFT:
                                                                 case DIRECTION_RIGHT:
                                                                      entangled_block->pos_delta.x = block_instant_vel * dt;
                                                                      entangled_block->pos.decimal.x = entangled_block->pos.decimal.y;
                                                                      break;
                                                                 case DIRECTION_UP:
                                                                 case DIRECTION_DOWN:
                                                                      entangled_block->pos_delta.y = block_instant_vel * dt;
                                                                      entangled_block->pos.decimal.y = entangled_block->pos.decimal.x;
                                                                      break;
                                                                 }
                                                            }
                                                       }else{
                                                            auto stop_entangled_dir = direction_rotate_clockwise(entangled_move_dir_to_stop, result.collided_portal_rotations);

                                                            stop_block_colliding_with_entangled(block, move_dir_to_stop, &result);
                                                            stop_block_colliding_with_entangled(entangled_block, stop_entangled_dir, &entangle_result);

                                                            // TODO: compress this code, it's definitely used elsewhere
                                                            for(S16 p = 0; p < world.players.count; p++){
                                                                 auto* player = world.players.elements + p;
                                                                 if(player->prev_pushing_block == i || player->prev_pushing_block == block->entangle_index){
                                                                      player->push_time = 0.0f;
                                                                 }
                                                            }
                                                       }
                                                  }
                                             }
                                        }else{
                                             copy_block_collision_results(block, &result);
                                        }
                                   }else{
                                        copy_block_collision_results(block, &result);
                                   }
                              }
                         }

                         // get the current coord of the center of the block
                         Pixel_t center = block->pos.pixel + HALF_TILE_SIZE_PIXEL;
                         Coord_t coord = pixel_to_coord(center);

                         Coord_t skip_coords[DIRECTION_COUNT];
                         find_portal_adjacents_to_skip_collision_check(coord, world.interactive_qt, skip_coords);

                         // check for adjacent walls
                         if(block->vel.x > 0.0f){
                              if(check_direction_from_block_for_adjacent_walls(block, &world.tilemap, world.interactive_qt, skip_coords, DIRECTION_RIGHT)){
                                   stop_on_boundary_x = true;
                              }
                         }else if(block->vel.x < 0.0f){
                              if(check_direction_from_block_for_adjacent_walls(block, &world.tilemap, world.interactive_qt, skip_coords, DIRECTION_LEFT)){
                                   stop_on_boundary_x = true;
                              }
                         }

                         if(block->vel.y > 0.0f){
                              if(check_direction_from_block_for_adjacent_walls(block, &world.tilemap, world.interactive_qt, skip_coords, DIRECTION_UP)){
                                   stop_on_boundary_y = true;
                              }
                         }else if(block->vel.y < 0.0f){
                              if(check_direction_from_block_for_adjacent_walls(block, &world.tilemap, world.interactive_qt, skip_coords, DIRECTION_DOWN)){
                                   stop_on_boundary_y = true;
                              }
                         }

                         // TODO: we need to maintain a list of these blocks pushed i guess in the future
                         Block_t* block_pushed = nullptr;
                         for(S16 p = 0; p < world.players.count; p++){
                              Player_t* player = world.players.elements + p;
                              if(player->prev_pushing_block >= 0){
                                   block_pushed = world.blocks.elements + player->prev_pushing_block;
                              }
                         }

                         // this instance of last_block_pushed is to keep the pushing smooth and not have it stop at the tile boundaries
                         if(block != block_pushed && !block_on_ice(block->pos, block->pos_delta, &world.tilemap, world.interactive_qt)){
                              if(block_pushed && blocks_are_entangled(block_pushed, block, &world.blocks)){
                                   Block_t* entangled_block = block_pushed;

                                   S8 rotations_between = blocks_rotations_between(block, entangled_block);

                                   auto coast_horizontal = entangled_block->coast_horizontal;
                                   auto coast_vertical = entangled_block->coast_vertical;

                                   // swap horizontal and vertical for odd rotations
                                   if((rotations_between % 2) == 1){
                                        coast_horizontal = entangled_block->coast_vertical;
                                        coast_vertical = entangled_block->coast_horizontal;
                                   }

                                   if(coast_horizontal == BLOCK_COAST_PLAYER){
                                        auto vel_mask = vec_direction_mask(block->vel);
                                        Vec_t entangled_vel {entangled_block->vel.x, 0};

                                        // swap x and y if rotations between is odd
                                        if((rotations_between % 2) == 1) entangled_vel = Vec_t{0, entangled_block->vel.y};

                                        Direction_t entangled_move_dir = vec_direction(entangled_vel);
                                        Direction_t move_dir = direction_rotate_clockwise(entangled_move_dir, rotations_between);

                                        // TODO: compress this code with the duplicate code below if it matches?
                                        switch(move_dir){
                                        default:
                                             break;
                                        case DIRECTION_LEFT:
                                             if(vel_mask & DIRECTION_MASK_RIGHT){
                                                  stop_on_boundary_x = true;
                                             }else if(vel_mask & DIRECTION_MASK_UP || vel_mask & DIRECTION_MASK_DOWN){
                                                  stop_on_boundary_y = true;
                                             }
                                             break;
                                        case DIRECTION_RIGHT:
                                             if(vel_mask & DIRECTION_MASK_LEFT){
                                                  stop_on_boundary_x = true;
                                             }else if(vel_mask & DIRECTION_MASK_UP || vel_mask & DIRECTION_MASK_DOWN){
                                                  stop_on_boundary_y = true;
                                             }
                                             break;
                                        case DIRECTION_UP:
                                             if(vel_mask & DIRECTION_MASK_DOWN){
                                                  stop_on_boundary_y = true;
                                             }else if(vel_mask & DIRECTION_MASK_LEFT || vel_mask & DIRECTION_MASK_RIGHT){
                                                  stop_on_boundary_x = true;
                                             }
                                             break;
                                        case DIRECTION_DOWN:
                                             if(vel_mask & DIRECTION_MASK_UP){
                                                  stop_on_boundary_y = true;
                                             }else if(vel_mask & DIRECTION_MASK_LEFT || vel_mask & DIRECTION_MASK_RIGHT){
                                                  stop_on_boundary_x = true;
                                             }
                                             break;
                                        }
                                   }else{
                                        if(block->vel.x != 0){
                                             stop_on_boundary_x = true;
                                        }
                                   }

                                   if(coast_vertical == BLOCK_COAST_PLAYER){
                                        auto vel_mask = vec_direction_mask(block->vel);
                                        Vec_t entangled_vel {0, entangled_block->vel.y};

                                        // swap x and y if rotations between is odd
                                        if((rotations_between % 2) == 1) entangled_vel = Vec_t{entangled_block->vel.x, 0};

                                        Direction_t entangled_move_dir = vec_direction(entangled_vel);
                                        Direction_t move_dir = direction_rotate_clockwise(entangled_move_dir, rotations_between);

                                        switch(move_dir){
                                        default:
                                             break;
                                        case DIRECTION_LEFT:
                                             if(vel_mask & DIRECTION_MASK_RIGHT){
                                                  stop_on_boundary_x = true;
                                             }else if(vel_mask & DIRECTION_MASK_UP || vel_mask & DIRECTION_MASK_DOWN){
                                                  stop_on_boundary_y = true;
                                             }
                                             break;
                                        case DIRECTION_RIGHT:
                                             if(vel_mask & DIRECTION_MASK_LEFT){
                                                  stop_on_boundary_x = true;
                                             }else if(vel_mask & DIRECTION_MASK_UP || vel_mask & DIRECTION_MASK_DOWN){
                                                  stop_on_boundary_y = true;
                                             }
                                             break;
                                        case DIRECTION_UP:
                                             if(vel_mask & DIRECTION_MASK_DOWN){
                                                  stop_on_boundary_y = true;
                                             }else if(vel_mask & DIRECTION_MASK_LEFT || vel_mask & DIRECTION_MASK_RIGHT){
                                                  stop_on_boundary_x = true;
                                             }
                                             break;
                                        case DIRECTION_DOWN:
                                             if(vel_mask & DIRECTION_MASK_UP){
                                                  stop_on_boundary_y = true;
                                             }else if(vel_mask & DIRECTION_MASK_LEFT || vel_mask & DIRECTION_MASK_RIGHT){
                                                  stop_on_boundary_x = true;
                                             }
                                             break;
                                        }
                                   }else{
                                        if(block->vel.y != 0) stop_on_boundary_y = true;
                                   }
                              }else{
                                   if(block->vel.x != 0) stop_on_boundary_x = true;
                                   if(block->vel.y != 0) stop_on_boundary_y = true;
                              }
                         }

                         auto final_pos = block->pos + block->pos_delta;

                         if(stop_on_boundary_x){
                              // stop on tile boundaries separately for each axis
                              S16 boundary_x = range_passes_tile_boundary(block->pos.pixel.x, final_pos.pixel.x, block->started_on_pixel_x);
                              if(boundary_x){
                                   collided = true;

                                   block->successfully_moved = false;

                                   block->stop_on_pixel_x = boundary_x;
                                   block->accel.x = 0;
                                   block->vel.x = 0;
                                   block->pos_delta.x = 0;
                                   block->horizontal_move.state = MOVE_STATE_IDLING;
                              }
                         }

                         if(stop_on_boundary_y){
                              S16 boundary_y = range_passes_tile_boundary(block->pos.pixel.y, final_pos.pixel.y, block->started_on_pixel_y);
                              if(boundary_y){
                                   collided = true;

                                   block->successfully_moved = false;

                                   block->stop_on_pixel_y = boundary_y;
                                   block->accel.y = 0;
                                   block->vel.y = 0;
                                   block->pos_delta.y = 0;
                                   block->vertical_move.state = MOVE_STATE_IDLING;
                              }
                         }

                         auto* portal = block_is_teleporting(block, world.interactive_qt);

                         // is the block teleporting and it hasn't been cloning ?
                         if(portal && block->clone_start.x == 0){
                              // at the first instant of the block teleporting, check if we should create an entangled_block

                              PortalExit_t portal_exits = find_portal_exits(portal->coord, &world.tilemap, world.interactive_qt);
                              S8 clone_id = 0;
                              for (auto &direction : portal_exits.directions) {
                                   for(int p = 0; p < direction.count; p++){
                                        if(direction.coords[p] == portal->coord) continue;

                                        if(clone_id == 0){
                                             block->clone_id = clone_id;
                                        }else{
                                             S16 new_block_index = world.blocks.count;
                                             S16 old_block_index = (S16)(block - world.blocks.elements);

                                             if(resize(&world.blocks, world.blocks.count + (S16)(1))){
                                                  // a resize will kill our block ptr, so we gotta update it
                                                  block = world.blocks.elements + old_block_index;
                                                  block->clone_start = portal->coord;

                                                  Block_t* entangled_block = world.blocks.elements + new_block_index;

                                                  *entangled_block = *block;
                                                  entangled_block->clone_id = clone_id;

                                                  // the magic
                                                  if(block->entangle_index == -1){
                                                       entangled_block->entangle_index = old_block_index;
                                                  }else{
                                                       entangled_block->entangle_index = block->entangle_index;
                                                  }

                                                  block->entangle_index = new_block_index;

                                                  // update quad tree now that we have resized the world
                                                  quad_tree_free(world.block_qt);
                                                  world.block_qt = quad_tree_build(&world.blocks);
                                             }
                                        }

                                        clone_id++;
                                   }
                              }

                         // if we didn't find a portal but we have been cloning, we are done cloning! We either need to kill the clone or complete it
                         }else if(!portal && block->clone_start.x > 0 &&
                                  block->entangle_index < world.blocks.count){
                              // TODO: do we need to update the block quad tree here?
                              auto block_move_dir = vec_direction(block->pos_delta);
                              auto block_from_coord = block_get_coord(block);
                              if(block_move_dir != DIRECTION_COUNT) block_from_coord -= block_move_dir;

                              if(block_from_coord == block->clone_start){
                                   // in this instance, despawn the clone
                                   // NOTE: I think this relies on new entangle blocks being
                                   S16 block_index = (S16)(block - world.blocks.elements);
                                   remove(&world.blocks, block->entangle_index);

                                   // TODO: This could lead to a subtle bug where our block index is no longer correct
                                   // TODO: because this was the last index in the list which got swapped to our index
                                   // update ptr since we could have resized
                                   block = world.blocks.elements + block_index;

                                   update_blocks_count--;

                                   // if our entangled block was not the last element, then it was replaced by another
                                   if(block->entangle_index < world.blocks.count){
                                        Block_t* replaced_block = world.blocks.elements + block->entangle_index;
                                        if(replaced_block->entangle_index >= 0){
                                             // update the block's entangler that moved into our entangled blocks spot
                                             Block_t* replaced_block_entangler = world.blocks.elements + replaced_block->entangle_index;
                                             replaced_block_entangler->entangle_index = (S16)(replaced_block - world.blocks.elements);
                                        }
                                   }

                                   block->entangle_index = -1;
                              }else{
                                   assert(block->entangle_index >= 0);

                                   // great news everyone, the clone was a success

                                   // find the block(s) we were cloning
                                   S16 entangle_index = block->entangle_index;
                                   while(entangle_index != i && entangle_index != -1){
                                        Block_t* entangled_block = world.blocks.elements + entangle_index;
                                        if(entangled_block->clone_start.x != 0){
                                             entangled_block->clone_id = 0;
                                             entangled_block->clone_start = Coord_t{};
                                        }

                                        entangle_index = entangled_block->entangle_index;
                                   }

                                   // we reset clone_start down a little further
                                   block->clone_id = 0;

                                   // turn off the circuit
                                   activate(&world, block->clone_start);
                                   auto* src_portal = quad_tree_find_at(world.interactive_qt, block->clone_start.x, block->clone_start.y);
                                   if(is_active_portal(src_portal)){
                                        src_portal->portal.on = false;
                                   }

                              }

                              block->clone_start = Coord_t{};
                         }
                    }

                    // player movement
                    S16 update_player_count = world.players.count; // save due to adding/removing players
                    for(S16 i = 0; i < update_player_count; i++){
                         Player_t* player = world.players.elements + i;

                         player->successfully_moved = true;

                         Coord_t skip_coord[DIRECTION_COUNT];
                         Coord_t player_coord = pos_to_coord(player->pos + player->pos_delta);

                         find_portal_adjacents_to_skip_collision_check(player_coord, world.interactive_qt, skip_coord);

                         auto result = move_player_through_world(player->pos, player->vel, player->pos_delta, player->face,
                                                                 player->clone_instance, i, player->pushing_block,
                                                                 player->pushing_block_dir, player->pushing_block_rotation,
                                                                 skip_coord, &world);

                         if(player->teleport){
                              Coord_t teleport_player_coord = pos_to_coord(player->teleport_pos + player->teleport_pos_delta);

                              find_portal_adjacents_to_skip_collision_check(teleport_player_coord, world.interactive_qt, skip_coord);

                              auto teleport_result = move_player_through_world(player->teleport_pos, player->vel, player->teleport_pos_delta, player->teleport_face,
                                                                               player->clone_instance, i, player->pushing_block,
                                                                               player->pushing_block_dir, player->pushing_block_rotation, skip_coord, &world);

                              if(teleport_result.collided){
                                   collided = true;
                                   player->successfully_moved = false;
                              }
                              if(teleport_result.resetting) resetting = true;
                              player->teleport_pos_delta = teleport_result.pos_delta;
                              player->teleport_pushing_block = teleport_result.pushing_block;
                              player->teleport_pushing_block_dir = teleport_result.pushing_block_dir;
                              player->teleport_pushing_block_rotation = teleport_result.pushing_block_rotation;
                         }

                         if(result.collided){
                              collided = true;
                              player->successfully_moved = false;
                         }
                         if(result.resetting) resetting = true;
                         player->pos_delta = result.pos_delta;
                         player->pushing_block = result.pushing_block;
                         player->pushing_block_dir = result.pushing_block_dir;
                         player->pushing_block_rotation = result.pushing_block_rotation;

                         auto* portal = player_is_teleporting(player, world.interactive_qt);

                         if(portal && player->clone_start.x == 0){
                              // at the first instant of the block teleporting, check if we should create an entangled_block

                              PortalExit_t portal_exits = find_portal_exits(portal->coord, &world.tilemap, world.interactive_qt);
                              S8 count = portal_exit_count(&portal_exits);
                              if(count >= 3){ // src portal, dst portal, clone portal
                                   world.clone_instance++;

                                   S8 clone_id = 0;
                                   for (auto &direction : portal_exits.directions) {
                                        for(int p = 0; p < direction.count; p++){
                                             if(direction.coords[p] == portal->coord) continue;

                                             if(clone_id == 0){
                                                  player->clone_id = clone_id;
                                                  player->clone_instance = world.clone_instance;
                                             }else{
                                                  S16 new_player_index = world.players.count;

                                                  if(resize(&world.players, world.players.count + (S16)(1))){
                                                       // a resize will kill our player ptr, so we gotta update it
                                                       player = world.players.elements + i;
                                                       player->clone_start = portal->coord;

                                                       Player_t* new_player = world.players.elements + new_player_index;
                                                       *new_player = *player;
                                                       new_player->clone_id = clone_id;
                                                  }
                                             }

                                             clone_id++;
                                        }
                                   }
                              }
                         }else if(!portal && player->clone_start.x > 0){
                              auto clone_portal_center = coord_to_pixel_at_center(player->clone_start);
                              F64 player_distance_from_portal = pixel_distance_between(clone_portal_center, player->pos.pixel);
                              bool from_clone_start = (player_distance_from_portal < TILE_SIZE_IN_PIXELS);

                              if(from_clone_start){
                                   // loop across all players after this one
                                   for(S16 p = 0; p < world.players.count; p++){
                                        if(p == i) continue;
                                        Player_t* other_player = world.players.elements + p;
                                        if(other_player->clone_instance == player->clone_instance){
                                             // TODO: I think I may have a really subtle bug here where we actually move
                                             // TODO: the i'th player around because it was the last in the array
                                             remove(&world.players, p);

                                             // update ptr since we could have resized
                                             player = world.players.elements + i;

                                             update_player_count--;
                                        }
                                   }
                              }else{
                                   for(S16 p = 0; p < world.players.count; p++){
                                        if(p == i) continue;
                                        Player_t* other_player = world.players.elements + p;
                                        if(other_player->clone_instance == player->clone_instance){
                                             other_player->clone_id = 0;
                                             other_player->clone_instance = 0;
                                             other_player->clone_start = Coord_t{};
                                        }
                                   }

                                   // turn off the circuit
                                   activate(&world, player->clone_start);
                                   auto* src_portal = quad_tree_find_at(world.interactive_qt, player->clone_start.x, player->clone_start.y);
                                   if(is_active_portal(src_portal)) src_portal->portal.on = false;
                              }

                              player->clone_id = 0;
                              player->clone_instance = 0;
                              player->clone_start = Coord_t{};
                         }

                         Interactive_t* interactive = quad_tree_find_at(world.interactive_qt, player_coord.x, player_coord.y);
                         if(interactive && interactive->type == INTERACTIVE_TYPE_CLONE_KILLER){
                              if(i == 0){
                                   resize(&world.players, 1);
                                   update_player_count = 1;
                              }else{
                                   resetting = true;
                              }
                         }
                    }

                    collision_attempts++;
               }

               // finalize positions
               for(S16 i = 0; i < world.players.count; i++){
                    auto player = world.players.elements + i;

                    if(!player->successfully_moved){
                         player->pos_delta = vec_zero();
                         player->prev_vel = vec_zero();
                         player->vel = vec_zero();
                         player->accel = vec_zero();
                         continue;
                    }

                    if(player->teleport){
                         player->pos = player->teleport_pos + player->teleport_pos_delta;
                         player->pos_delta = player->teleport_pos_delta;

                         player->face = player->teleport_face;
                         player->vel = vec_rotate_quadrants_clockwise(player->vel, player->teleport_rotation);
                         player->accel = vec_rotate_quadrants_clockwise(player->accel, player->teleport_rotation);
                         player->pushing_block = player->teleport_pushing_block;
                         player->pushing_block_dir = player->teleport_pushing_block_dir;
                         player->pushing_block_rotation = player->teleport_pushing_block_rotation;
                         player->rotation = (player->rotation + player->teleport_rotation) % DIRECTION_COUNT;

                         auto first_player = world.players.elements + 0;

                         // set everyone's rotation relative to the first player
                         if(i != 0) player->rotation = direction_rotations_between((Direction_t)(player->rotation), (Direction_t)(first_player->rotation));

                         // set rotations for each direction the player wants to move
                         for(S8 d = 0; d < DIRECTION_COUNT; d++){
                              if(player_action.move[d]){
                                   player->move_rotation[d] = (player->move_rotation[d] + first_player->teleport_rotation) % DIRECTION_COUNT;
                              }
                         }
                    }else{
                         player->pos += player->pos_delta;
                    }
               }

               // reset the first player's rotation
               if(world.players.count > 0){
                    world.players.elements[0].rotation = 0;
               }

               for(S16 i = 0; i < world.blocks.count; i++){
                    Block_t* block = world.blocks.elements + i;

                    if(!block->successfully_moved){
                         // TODO: its probably not the best thing to stop this in all directions, we probably only
                         //       want to stop it in the direction that it couldn't resolve collision in
                         memset(block, 0, sizeof(GridMotion_t));
                         continue;
                    }

                    Position_t final_pos;

                    if(block->teleport){
                         final_pos = block->teleport_pos + block->teleport_pos_delta;

                         block->pos_delta = block->teleport_pos_delta;
                         block->vel = block->teleport_vel;
                         block->accel = block->teleport_accel;
                         block->stop_on_pixel_x = block->teleport_stop_on_pixel_x;
                         block->stop_on_pixel_y = block->teleport_stop_on_pixel_y;
                         block->rotation = (block->rotation + block->teleport_rotation) % static_cast<U8>(DIRECTION_COUNT);
                         block->horizontal_move = block->teleport_horizontal_move;
                         block->vertical_move = block->teleport_vertical_move;

                         if(block->rotation % 2 != 0){
                              F32 tmp = block->accel_magnitudes.x;
                              block->accel_magnitudes.x = block->accel_magnitudes.y;
                              block->accel_magnitudes.y = tmp;
                         }
                    }else{
                         final_pos = block->pos + block->pos_delta;
                    }

                    // finalize position for each component, stopping on a pixel boundary if we have to
                    if(block->stop_on_pixel_x != 0){
                         block->pos.pixel.x = block->stop_on_pixel_x;
                         block->pos.decimal.x = 0;
                         block->stop_on_pixel_x = 0;

                         block->stopped_by_player_horizontal = false;
                    }else{
                         block->pos.pixel.x = final_pos.pixel.x;
                         block->pos.decimal.x = final_pos.decimal.x;
                    }

                    if(block->stop_on_pixel_y != 0){
                         block->pos.pixel.y = block->stop_on_pixel_y;
                         block->pos.decimal.y = 0;
                         block->stop_on_pixel_y = 0;

                         block->stopped_by_player_vertical = false;
                    }else{
                         block->pos.pixel.y = final_pos.pixel.y;
                         block->pos.decimal.y = final_pos.decimal.y;
                    }

#if DEBUG_FILE
                    if(i == 2){
                         fprintf(debug_file, "%ld %d %d %f %f hm %s %s %f vm %s %s %f\n", frame_count,
                                 block->pos.pixel.x, block->pos.pixel.y, block->pos.decimal.x, block->pos.decimal.y,
                                 move_state_to_string(block->horizontal_move.state), move_sign_to_string(block->horizontal_move.sign), block->horizontal_move.distance,
                                 move_state_to_string(block->vertical_move.state), move_sign_to_string(block->vertical_move.sign), block->vertical_move.distance);
                         fflush(debug_file);
                    }
#endif
               }

               // have player push block
               for(S16 i = 0; i < world.players.count; i++){
                    auto player = world.players.elements + i;
                    if(player->prev_pushing_block >= 0 && player->prev_pushing_block == player->pushing_block){
                         Block_t* block_to_push = world.blocks.elements + player->prev_pushing_block;
                         DirectionMask_t block_move_dir_mask = vec_direction_mask(block_to_push->vel);
                         auto push_block_dir = player->pushing_block_dir;
                         if(block_to_push->teleport) push_block_dir = direction_rotate_clockwise(push_block_dir, block_to_push->teleport_rotation);

                         if(direction_in_mask(direction_mask_opposite(block_move_dir_mask), push_block_dir)){
                              // if the player is pushing against a block moving towards them, the block wins
                              player->push_time = 0;
                              player->pushing_block = -1;
                         }else if(direction_in_mask(block_move_dir_mask, push_block_dir)){
                              block_to_push->cur_push_mask = direction_mask_add(block_to_push->cur_push_mask, push_block_dir);
                         }else{
                              F32 save_push_time = player->push_time;

                              // TODO: get back to this once we improve our demo tools
                              player->push_time += dt;
                              if(player->push_time > BLOCK_PUSH_TIME){

                                   // if this is the frame that causes the block to be pushed, make a commit
                                   if(save_push_time <= BLOCK_PUSH_TIME) undo_commit(&undo, &world.players, &world.tilemap, &world.blocks, &world.interactives);

                                   bool pushed = block_push(block_to_push, push_block_dir, &world, false);

                                   if(!pushed){
                                        player->push_time = 0.0f;
                                   }else if(block_to_push->entangle_index >= 0 && block_to_push->entangle_index < world.blocks.count){
                                        player->pushing_block_dir = push_block_dir;
                                        push_entangled_block(block_to_push, &world, push_block_dir, false);
                                   }

                                   if(block_to_push->pos.z > 0) player->push_time = -0.5f; // TODO: wtf is this line?
                              }
                         }
                    }else{
                         player->push_time = 0;
                    }
               }

               // illuminate and spread ice
               for(S16 i = 0; i < world.blocks.count; i++){
                    Block_t* block = world.blocks.elements + i;
                    if(block->element == ELEMENT_FIRE){
                         illuminate(block_get_coord(block), 255, &world);
                    }else if(block->element == ELEMENT_ICE){
                         auto block_coord = block_get_coord(block);
                         spread_ice(block_coord, 1, &world);
                    }
               }

               // melt ice in a separate pass
               for(S16 i = 0; i < world.blocks.count; i++){
                    Block_t* block = world.blocks.elements + i;
                    if(block->element == ELEMENT_FIRE){
                         melt_ice(block_get_coord(block), 1, &world);
                    }
               }

               // update light and ice detectors
               for(S16 i = 0; i < world.interactives.count; i++){
                    update_light_and_ice_detectors(world.interactives.elements + i, &world);
               }

               if(resetting){
                    reset_timer += dt;
                    if(reset_timer >= RESET_TIME){
                         resetting = false;
                         load_map_number_map(map_number, &world, &undo, &player_start, &player_action);
                    }
               }else{
                    reset_timer -= dt;
                    if(reset_timer <= 0) reset_timer = 0;
               }
          }

          if((suite && !show_suite) || demo.seek_frame >= 0) continue;

          // begin drawing
          Position_t screen_camera = camera - Vec_t{0.5f, 0.5f} + Vec_t{HALF_TILE_SIZE, HALF_TILE_SIZE};

          Coord_t min = pos_to_coord(screen_camera);
          Coord_t max = min + Coord_t{ROOM_TILE_SIZE, ROOM_TILE_SIZE};
          min = coord_clamp_zero_to_dim(min, world.tilemap.width - (S16)(1), world.tilemap.height - (S16)(1));
          max = coord_clamp_zero_to_dim(max, world.tilemap.width - (S16)(1), world.tilemap.height - (S16)(1));
          Position_t tile_bottom_left = coord_to_pos(min);
          Vec_t camera_offset = pos_to_vec(tile_bottom_left - screen_camera);

          glClear(GL_COLOR_BUFFER_BIT);

          // draw flats
          glBindTexture(GL_TEXTURE_2D, theme_texture);
          glBegin(GL_QUADS);
          glColor3f(1.0f, 1.0f, 1.0f);

          for(S16 y = max.y; y >= min.y; y--){
               for(S16 x = min.x; x <= max.x; x++){
                    Vec_t tile_pos {(F32)(x - min.x) * TILE_SIZE + camera_offset.x,
                                    (F32)(y - min.y) * TILE_SIZE + camera_offset.y};

                    Tile_t* tile = world.tilemap.tiles[y] + x;
                    Interactive_t* interactive = quad_tree_find_at(world.interactive_qt, x, y);
                    if(is_active_portal(interactive)){
                         Coord_t coord {x, y};
                         PortalExit_t portal_exits = find_portal_exits(coord, &world.tilemap, world.interactive_qt);

                         for(S8 d = 0; d < DIRECTION_COUNT; d++){
                              for(S8 i = 0; i < portal_exits.directions[d].count; i++){
                                   if(portal_exits.directions[d].coords[i] == coord) continue;
                                   Coord_t portal_coord = portal_exits.directions[d].coords[i] + direction_opposite((Direction_t)(d));
                                   Tile_t* portal_tile = world.tilemap.tiles[portal_coord.y] + portal_coord.x;
                                   Interactive_t* portal_interactive = quad_tree_find_at(world.interactive_qt, portal_coord.x, portal_coord.y);
                                   U8 portal_rotations = portal_rotations_between((Direction_t)(d), interactive->portal.face);
                                   draw_flats(tile_pos, portal_tile, portal_interactive, theme_texture, portal_rotations);
                              }
                         }
                    }else{
                         draw_flats(tile_pos, tile, interactive, theme_texture, 0);
                    }
               }
          }

          bool* draw_players = (bool*)malloc((size_t)(world.players.count));

          for(S16 y = max.y; y >= min.y; y--){
               for(S16 x = min.x; x <= max.x; x++){
                    Vec_t tile_pos {(F32)(x - min.x) * TILE_SIZE + camera_offset.x,
                                    (F32)(y - min.y) * TILE_SIZE + camera_offset.y};

                    Coord_t coord {x, y};
                    Interactive_t* interactive = quad_tree_find_at(world.interactive_qt, coord.x, coord.y);

                    if(is_active_portal(interactive)){
                         PortalExit_t portal_exits = find_portal_exits(coord, &world.tilemap, world.interactive_qt);

                         for(S8 d = 0; d < DIRECTION_COUNT; d++){
                              for(S8 i = 0; i < portal_exits.directions[d].count; i++){
                                   if(portal_exits.directions[d].coords[i] == coord) continue;
                                   Coord_t portal_coord = portal_exits.directions[d].coords[i] + direction_opposite((Direction_t)(d));
                                   Rect_t coord_rect = rect_surrounding_coord(portal_coord);

                                   S16 block_count = 0;
                                   Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];

                                   quad_tree_find_in(world.block_qt, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

                                   Interactive_t* portal_interactive = quad_tree_find_at(world.interactive_qt, portal_coord.x, portal_coord.y);

                                   U8 portal_rotations = portal_rotations_between((Direction_t)(d), interactive->portal.face);
                                   Pixel_t portal_center_pixel = coord_to_pixel_at_center(portal_coord);
                                   for(S16 p = 0; p < world.players.count; p++){
                                        draw_players[p] = (pixel_distance_between(portal_center_pixel, world.players.elements[p].pos.pixel) <= 20);
                                   }

                                   sort_blocks_by_height(blocks, block_count);

                                   draw_solids(tile_pos, portal_interactive, blocks, block_count, &world.players, draw_players,
                                               screen_camera, theme_texture, player_texture, portal_coord, coord,
                                               portal_rotations, &world.tilemap, world.interactive_qt);
                              }
                         }

                         draw_interactive(interactive, tile_pos, coord, &world.tilemap, world.interactive_qt);
                    }
               }
          }

          for(S16 y = max.y; y >= min.y; y--){
               for(S16 x = min.x; x <= max.x; x++){
                    Tile_t* tile = world.tilemap.tiles[y] + x;
                    if(tile && tile->id >= 16){
                         Vec_t tile_pos {(F32)(x - min.x) * TILE_SIZE + camera_offset.x,
                                         (F32)(y - min.y) * TILE_SIZE + camera_offset.y};
                         draw_tile_id(tile->id, tile_pos);
                    }
               }
          }

          for(S16 y = max.y; y >= min.y; y--){
               for(S16 p = 0; p < world.players.count; p++){
                    draw_players[p] = (pos_to_coord(world.players.elements[p].pos).y == y);
               }

               for(S16 x = min.x; x <= max.x; x++){
                    Coord_t coord {x, y};
                    Rect_t coord_rect = rect_surrounding_coord(coord);
                    Vec_t tile_pos {(F32)(x - min.x) * TILE_SIZE + camera_offset.x,
                                    (F32)(y - min.y) * TILE_SIZE + camera_offset.y};

                    S16 block_count = 0;
                    Block_t* blocks[BLOCK_QUAD_TREE_MAX_QUERY];
                    quad_tree_find_in(world.block_qt, coord_rect, blocks, &block_count, BLOCK_QUAD_TREE_MAX_QUERY);

                    Interactive_t* interactive = quad_tree_find_at(world.interactive_qt, x, y);

                    sort_blocks_by_height(blocks, block_count);

                    draw_solids(tile_pos, interactive, blocks, block_count, &world.players, draw_players, screen_camera,
                                theme_texture, player_texture, coord, Coord_t{-1, -1}, 0, &world.tilemap, world.interactive_qt);

               }

               // draw arrows
               static Vec_t arrow_tip_offset[DIRECTION_COUNT] = {
                    {0.0f,               9.0f * PIXEL_SIZE},
                    {8.0f * PIXEL_SIZE,  16.0f * PIXEL_SIZE},
                    {16.0f * PIXEL_SIZE, 9.0f * PIXEL_SIZE},
                    {8.0f * PIXEL_SIZE,  0.0f * PIXEL_SIZE},
               };

               for(S16 a = 0; a < ARROW_ARRAY_MAX; a++){
                    Arrow_t* arrow = world.arrows.arrows + a;
                    if(!arrow->alive) continue;
                    if((arrow->pos.pixel.y / TILE_SIZE_IN_PIXELS) != y) continue;

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
                    Vec_t dim {TILE_SIZE, TILE_SIZE};
                    Vec_t tex_dim {ARROW_FRAME_WIDTH, ARROW_FRAME_HEIGHT};
                    draw_screen_texture(arrow_vec, tex_vec, dim, tex_dim);

                    arrow_vec.y += (arrow->pos.z * PIXEL_SIZE);

                    S8 y_frame = 0;
                    if(arrow->element) y_frame = (S8)(2 + ((arrow->element - 1) * 4));

                    tex_vec = arrow_frame(arrow->face, y_frame);
                    draw_screen_texture(arrow_vec, tex_vec, dim, tex_dim);

                    glEnd();

                    glBindTexture(GL_TEXTURE_2D, theme_texture);
                    glBegin(GL_QUADS);
                    glColor3f(1.0f, 1.0f, 1.0f);
               }
          }

          free(draw_players);

          glEnd();

#if 0
          // light
          glBindTexture(GL_TEXTURE_2D, 0);
          glBegin(GL_QUADS);
          for(S16 y = min.y; y <= max.y; y++){
               for(S16 x = min.x; x <= max.x; x++){
                    Tile_t* tile = world.tilemap.tiles[y] + x;

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
          draw_selection(player_start, player_start, screen_camera, 0.0f, 1.0f, 0.0f);

          // editor
          draw_editor(&editor, &world, screen_camera, mouse_screen, theme_texture, text_texture);

          if(reset_timer >= 0.0f){
               glBegin(GL_QUADS);
               glColor4f(0.0f, 0.0f, 0.0f, reset_timer / RESET_TIME);
               glVertex2f(0, 0);
               glVertex2f(0, 1);
               glVertex2f(1, 1);
               glVertex2f(1, 0);
               glEnd();
          }

          if(demo.mode == DEMO_MODE_PLAY){
               F32 demo_pct = (F32)(frame_count) / (F32)(demo.last_frame);
               Quad_t pct_bar_quad = {pct_bar_outline_quad.left, pct_bar_outline_quad.bottom, demo_pct, pct_bar_outline_quad.top};
               draw_quad_filled(&pct_bar_quad, 255.0f, 255.0f, 255.0f);
               draw_quad_wireframe(&pct_bar_outline_quad, 255.0f, 255.0f, 255.0f);

               char buffer[64];
               snprintf(buffer, 64, "F: %ld/%ld", frame_count, demo.last_frame);

               glBindTexture(GL_TEXTURE_2D, text_texture);
               glBegin(GL_QUADS);

               Vec_t text_pos {0.005f, 0.965f};

               glColor3f(0.0f, 0.0f, 0.0f);
               draw_text(buffer, text_pos + Vec_t{0.002f, -0.002f});

               glColor3f(1.0f, 1.0f, 1.0f);
               draw_text(buffer, text_pos);

               glEnd();
          }

          SDL_GL_SwapWindow(window);
     }

     switch(demo.mode){
     default:
          break;
     case DEMO_MODE_RECORD:
          player_action_perform(&player_action, &world.players, PLAYER_ACTION_TYPE_END_DEMO, demo.mode, demo.file, frame_count);
          // save map and player position
          save_map_to_file(demo.file, player_start, &world.tilemap, &world.blocks, &world.interactives);
          switch(demo.version){
          default:
               break;
          case 1:
               fwrite(&world.players.elements->pos.pixel, sizeof(world.players.elements->pos.pixel), 1, demo.file);
               break;
          case 2:
          {
               fwrite(&world.players.count, sizeof(world.players.count), 1, demo.file);
               for(S16 p = 0; p < world.players.count; p++){
                    Player_t* player = world.players.elements + p;
                    fwrite(&player->pos.pixel, sizeof(player->pos.pixel), 1, demo.file);
               }
          } break;
          }
          fclose(demo.file);
          break;
     case DEMO_MODE_PLAY:
          fclose(demo.file);
          break;
     }

     quad_tree_free(world.interactive_qt);
     quad_tree_free(world.block_qt);

     destroy(&world.blocks);
     destroy(&world.interactives);
     destroy(&undo);
     destroy(&world.tilemap);
     destroy(&editor);

     if(!suite){
          glDeleteTextures(1, &theme_texture);
          glDeleteTextures(1, &player_texture);
          glDeleteTextures(1, &arrow_texture);
          glDeleteTextures(1, &text_texture);

          SDL_GL_DeleteContext(opengl_context);
          SDL_DestroyWindow(window);
          SDL_Quit();
     }

#if DEBUG_FILE
     fclose(debug_file);
#endif

     Log_t::destroy();
     return 0;
}
