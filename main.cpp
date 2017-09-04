#include <iostream>
#include <chrono>
#include <cstdint>
#include <cfloat>
#include <cassert>

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#define SWAP(a, b) {auto c = a; a = b; b = c;}

#define MAXIMUM(a, b)((a > b) ? (a) : (b))
#define MINIMUM(a, b)((a < b) ? (a) : (b))

#define CLAMP(var, minimum, maximum) \
     if(var < minimum){              \
          var = minimum;             \
     }else if(var > maximum){        \
          var = maximum;             \
     }

// evil-genius quality right herr
#define CASE_ENUM_RET_STR(e) case e: return #e;

#define ELEM_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))

#define PIXEL_SIZE .00367647f
#define TILE_SIZE (16.0f / 272.0f)
#define HALF_TILE_SIZE (TILE_SIZE * 0.5f)
#define TILE_SIZE_IN_PIXELS 16
#define HALF_TILE_SIZE_IN_PIXELS 8

#define ROOM_TILE_SIZE 17

typedef int8_t  S8;
typedef int16_t S16;
typedef int32_t S32;
typedef int64_t S64;

typedef uint8_t  U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

typedef float  F32;
typedef double F64;

enum Direction_t : U8{
     DIR_LEFT = 0,
     DIR_UP = 1,
     DIR_RIGHT = 2,
     DIR_DOWN = 3,
     DIR_COUNT = 4,
};

enum DirectionMask_t : U8{
     DM_NONE = 0,
     DM_LEFT = 1,
     DM_UP = 2,
     DM_RIGHT = 4,
     DM_DOWN = 8,
     DM_ALL = 15,
};

// 0  none
// 1  left
// 2  up
// 3  left | up
// 4  right
// 5  right | left
// 6  right | up
// 7  right | left | up
// 8  down
// 9  down | left
// 10 down | up
// 11 down | left | up
// 12 down | right
// 13 down | right | left
// 14 down | right | up
// 15 down | left | up | right

DirectionMask_t g_direction_mask_conversion[] = {
     DM_LEFT,
     DM_UP,
     DM_RIGHT,
     DM_DOWN,
     DM_NONE,
};

bool direction_in_mask(DirectionMask_t mask, Direction_t dir){
     if(g_direction_mask_conversion[dir] & mask){
          return true;
     }

     return false;
}

DirectionMask_t direction_to_direction_mask(Direction_t dir){
     assert(dir <= DIR_COUNT);
     return g_direction_mask_conversion[dir];
}

DirectionMask_t direction_mask_add(DirectionMask_t a, DirectionMask_t b){
     return (DirectionMask_t)(a | b); // C++ makes this annoying
}

DirectionMask_t direction_mask_add(DirectionMask_t a, int b){
     return (DirectionMask_t)(a | b); // C++ makes this annoying
}

DirectionMask_t direction_mask_add(DirectionMask_t mask, Direction_t dir){
     return (DirectionMask_t)(mask | direction_to_direction_mask(dir)); // C++ makes this annoying
}

DirectionMask_t direction_mask_remove(DirectionMask_t a, DirectionMask_t b){
     return (DirectionMask_t)(a & ~b); // C++ makes this annoying
}

DirectionMask_t direction_mask_remove(DirectionMask_t a, int b){
     return (DirectionMask_t)(a & ~b); // C++ makes this annoying
}

DirectionMask_t direction_mask_remove(DirectionMask_t mask, Direction_t dir){
     return (DirectionMask_t)(mask & ~direction_to_direction_mask(dir)); // C++ makes this annoying
}

Direction_t direction_opposite(Direction_t dir){return (Direction_t)(((int)(dir) + 2) % DIR_COUNT);}
bool direction_is_horizontal(Direction_t dir){return dir == DIR_LEFT || dir == DIR_RIGHT;}

U8 direction_rotations_between(Direction_t a, Direction_t b){
     if(a < b){
          return ((int)(a) + DIR_COUNT) - (int)(b);
     }

     return (int)(a) - (int)(b);
}

Direction_t direction_rotate_clockwise(Direction_t dir){
     U8 rot = (U8)(dir) + 1;
     rot %= DIR_COUNT;
     return (Direction_t)(rot);
}

Direction_t direction_rotate_clockwise(Direction_t dir, U8 times){
     for(U8 i = 0; i < times; ++i){
          dir = direction_rotate_clockwise(dir);
     }

     return dir;
}

DirectionMask_t direction_mask_rotate_clockwise(DirectionMask_t mask){
     // TODO: could probably just shift?
     S8 rot = DM_NONE;

     if(mask & DM_LEFT) rot |= DM_UP;
     if(mask & DM_UP) rot |= DM_RIGHT;
     if(mask & DM_RIGHT) rot |= DM_DOWN;
     if(mask & DM_DOWN) rot |= DM_LEFT;

     return (DirectionMask_t)(rot);
}

DirectionMask_t direction_mask_flip_horizontal(DirectionMask_t mask){
     S8 flip = DM_NONE;

     if(mask & DM_LEFT) flip |= DM_RIGHT;
     if(mask & DM_RIGHT) flip |= DM_LEFT;

     // keep the vertical components the same
     if(mask & DM_UP) flip |= DM_UP;
     if(mask & DM_DOWN) flip |= DM_DOWN;

     return (DirectionMask_t)(flip);
}

DirectionMask_t direction_mask_flip_vertical(DirectionMask_t mask){
     S8 flip = DM_NONE;

     if(mask & DM_UP) flip |= DM_DOWN;
     if(mask & DM_DOWN) flip |= DM_UP;

     // keep the horizontal components the same
     if(mask & DM_LEFT) flip |= DM_LEFT;
     if(mask & DM_RIGHT) flip |= DM_RIGHT;

     return (DirectionMask_t)(flip);
}

const char* direction_to_string(Direction_t dir){
     switch(dir){
     default:
          break;
     CASE_ENUM_RET_STR(DIR_LEFT)
     CASE_ENUM_RET_STR(DIR_UP)
     CASE_ENUM_RET_STR(DIR_RIGHT)
     CASE_ENUM_RET_STR(DIR_DOWN)
     CASE_ENUM_RET_STR(DIR_COUNT)
     }

     return "DIR_UNKNOWN";
}

struct Vec_t{
     F32 x;
     F32 y;
};

Vec_t operator+(Vec_t a, Vec_t b){return Vec_t{a.x + b.x, a.y + b.y};}
Vec_t operator-(Vec_t a, Vec_t b){return Vec_t{a.x - b.x, a.y - b.y};}

void operator+=(Vec_t& a, Vec_t b){a.x += b.x; a.y += b.y;}
void operator-=(Vec_t& a, Vec_t b){a.x -= b.x; a.y -= b.y;}

Vec_t operator*(Vec_t a, F32 s){return Vec_t{a.x * s, a.y * s};}
void operator*=(Vec_t& a, F32 s){a.x *= s; a.y *= s;}

float vec_dot(Vec_t a, Vec_t b){return a.x * b.x + a.y * b.y;}

Vec_t vec_negate(Vec_t a){return Vec_t{-a.x, -a.y};}
F32 vec_magnitude(Vec_t v){return (F32)(sqrt((v.x * v.x) + (v.y * v.y)));}

Vec_t vec_normalize(Vec_t a){
     F32 length = vec_magnitude(a);
     if(length <= FLT_EPSILON) return a;
     return Vec_t{a.x / length, a.y / length};
}

Vec_t vec_zero()  {return Vec_t{ 0.0f,  0.0f};}
Vec_t vec_left()  {return Vec_t{-1.0f,  0.0f};}
Vec_t vec_right() {return Vec_t{ 1.0f,  0.0f};}
Vec_t vec_up()    {return Vec_t{ 0.0f,  1.0f};}
Vec_t vec_down()  {return Vec_t{ 0.0f, -1.0f};}

void vec_set   (Vec_t* v, float x, float y)   {v->x = x; v->y = y;}
void vec_move  (Vec_t* v, float dx, float dy) {v->x += dx; v->y += dy;}
void vec_move_x(Vec_t* v, float dx)           {v->x += dx;}
void vec_move_y(Vec_t* v, float dy)           {v->y += dy;}

Vec_t vec_project_onto(Vec_t a, Vec_t b){
     // find the perpendicular vector
     Vec_t b_normal = vec_normalize(b);
     F32 along_b = vec_dot(a, b_normal);

     // clamp dot
     F32 b_magnitude = vec_magnitude(b);
     if(along_b < 0.0f){
          along_b = 0.0f;
     }else if(along_b > b_magnitude){
          along_b = b_magnitude;
     }

     // find the closest point
     return b_normal * along_b;
}

struct Pixel_t{
     S16 x;
     S16 y;
};

Pixel_t operator+(Pixel_t a, Pixel_t b){
     Pixel_t p;
     p.x = a.x + b.x;
     p.y = a.y + b.y;
     return p;
}

Pixel_t operator-(Pixel_t a, Pixel_t b){
     Pixel_t p;
     p.x = a.x - b.x;
     p.y = a.y - b.y;
     return p;
}

void operator+=(Pixel_t& a, Pixel_t b){
     a.x += b.x;
     a.y += b.y;
}

void operator-=(Pixel_t& a, Pixel_t b){
     a.x -= b.x;
     a.y -= b.y;
}

bool operator!=(Pixel_t& a, Pixel_t b){
     return (a.x != b.x || a.y != b.y);
}

struct Coord_t{
     S16 x;
     S16 y;
};

Coord_t coord_zero(){return Coord_t{0, 0};}
Coord_t coord_move(Coord_t c, Direction_t dir, S16 distance){
     switch ( dir ) {
     default:
          assert(!"invalid direction");
          break;
     case DIR_LEFT:
          c.x -= distance;
          break;
     case DIR_UP:
          c.y += distance;
          break;
     case DIR_RIGHT:
          c.x += distance;
          break;
     case DIR_DOWN:
          c.y -= distance;
          break;
     }

     return c;
}

Coord_t coord_clamp_zero_to_dim(Coord_t c, S16 width, S16 height){
     CLAMP(c.x, 0, width);
     CLAMP(c.y, 0, height);
     return c;
}

Coord_t operator+(Coord_t a, Coord_t b){return Coord_t{(S16)(a.x + b.x), (S16)(a.y + b.y)};}
Coord_t operator-(Coord_t a, Coord_t b){return Coord_t{(S16)(a.x - b.x), (S16)(a.y - b.y)};}

void operator+=(Coord_t& a, Coord_t b){a.x += b.x; a.y += b.y;}
void operator-=(Coord_t& a, Coord_t b){a.x -= b.x; a.y -= b.y;}

bool operator==(Coord_t a, Coord_t b){return (a.x == b.x && a.y == b.y);}
bool operator!=(Coord_t a, Coord_t b){return (a.x != b.x || a.y != b.y);}

Coord_t operator+(Coord_t c, Direction_t dir){return coord_move(c, dir, 1);}
Coord_t operator-(Coord_t c, Direction_t dir){return coord_move(c, direction_opposite(dir), 1);}
void operator+=(Coord_t& c, Direction_t dir){c = coord_move(c, dir, 1);}
void operator-=(Coord_t& c, Direction_t dir){c = coord_move(c, direction_opposite(dir), 1);}

void coord_set(Coord_t* c, S16 x, S16 y){c->x = x; c->y = y;}
void coord_move(Coord_t* c, S16 dx, S16 dy){c->x += dx; c->y += dy;}
void coord_move_x(Coord_t* c, S16 dx){c->x += dx;};
void coord_move_y(Coord_t* c, S16 dy){c->y += dy;}

bool coord_after(Coord_t a, Coord_t b){return b.y < a.y || (b.y == a.y && b.x < a.x);}

struct Position_t{
     Pixel_t pixel;
     S8 z;
     Vec_t decimal;
};

Position_t pixel_pos(S16 x, S16 y){
     Position_t p;
     p.pixel = {x, y};
     p.decimal = {0.0f, 0.0f};
     p.z = 0;
     return p;
}

Position_t pixel_pos(Pixel_t pixel){
     Position_t p;
     p.pixel = pixel;
     p.decimal = {0.0f, 0.0f};
     p.z = 0;
     return p;
}

void canonicalize(Position_t* position){
     if(position->decimal.x > PIXEL_SIZE){
          F32 pixels = (F32)(floor(position->decimal.x / PIXEL_SIZE));
          position->pixel.x += (S16)(pixels);
          position->decimal.x = (F32)(fmod(position->decimal.x, PIXEL_SIZE));
     }else if(position->decimal.x < 0.0f){
          F32 pixels = (F32)(floor(position->decimal.x / PIXEL_SIZE));
          position->pixel.x += (S16)(pixels);
          position->decimal.x = (F32)(fmod(position->decimal.x, PIXEL_SIZE));
          if(position->decimal.x < 0.0f) position->decimal.x += PIXEL_SIZE;
          else if(position->decimal.x == -0.0f) position->decimal.x = 0.0f;
     }

     if(position->decimal.y > PIXEL_SIZE){
          F32 pixels = (F32)(floor(position->decimal.y / PIXEL_SIZE));
          position->pixel.y += (S16)(pixels);
          position->decimal.y = (F32)(fmod(position->decimal.y, PIXEL_SIZE));
     }else if(position->decimal.y < 0.0f){
          F32 pixels = (F32)(floor(position->decimal.y / PIXEL_SIZE));
          position->pixel.y += (S16)(pixels);
          position->decimal.y = (F32)(fmod(position->decimal.y, PIXEL_SIZE));
          if(position->decimal.y < 0.0f) position->decimal.y += PIXEL_SIZE;
          else if(position->decimal.y == -0.0f) position->decimal.y = 0.0f;
     }
}

Position_t operator+(Position_t p, Vec_t v){
     p.decimal += v;
     canonicalize(&p);
     return p;
}

Position_t operator-(Position_t p, Vec_t v){
     p.decimal -= v;
     canonicalize(&p);
     return p;
}

void operator+=(Position_t& p, Vec_t v){
     p.decimal += v;
     canonicalize(&p);
}

void operator-=(Position_t& p, Vec_t v){
     p.decimal -= v;
     canonicalize(&p);
}

Position_t operator+(Position_t a, Position_t b){
     Position_t p;

     p.pixel = a.pixel + b.pixel;
     p.decimal = a.decimal + b.decimal;
     p.z = a.z + b.z;

     canonicalize(&p);
     return p;
}

Position_t operator-(Position_t a, Position_t b){
     Position_t p;

     p.pixel = a.pixel - b.pixel;
     p.decimal = a.decimal - b.decimal;
     p.z = a.z - b.z;

     canonicalize(&p);
     return p;
}

void operator+=(Position_t& a, Position_t b){
     a.pixel += b.pixel;
     a.decimal += b.decimal;
     canonicalize(&a);
}

void operator-=(Position_t& a, Position_t b){
     a.pixel -= b.pixel;
     a.decimal -= b.decimal;
     canonicalize(&a);
}

// NOTE: only call with small positions < 1
Position_t operator*(Position_t p, float scale){
     float x_value = (float)(p.pixel.x) * PIXEL_SIZE + p.decimal.x;
     x_value *= scale;
     p.pixel.x = 0;
     p.decimal.x = x_value;

     float y_value = (float)(p.pixel.y) * PIXEL_SIZE + p.decimal.y;
     y_value *= scale;
     p.pixel.y = 0;
     p.decimal.y = y_value;

     canonicalize(&p);
     return p;
}

Coord_t vec_to_coord(Vec_t v)
{
     Coord_t c;
     c.x = v.x / PIXEL_SIZE / TILE_SIZE_IN_PIXELS;
     c.y = v.y / PIXEL_SIZE / TILE_SIZE_IN_PIXELS;
     return c;
}

Vec_t coord_to_vec(Coord_t c)
{
     Vec_t v;
     v.x = (F32)(c.x * TILE_SIZE_IN_PIXELS) * PIXEL_SIZE;
     v.y = (F32)(c.y * TILE_SIZE_IN_PIXELS) * PIXEL_SIZE;
     return v;
}

Vec_t pos_to_vec(Position_t p)
{
     Vec_t v;

     v.x = (F32)(p.pixel.x) * PIXEL_SIZE + p.decimal.x;
     v.y = (F32)(p.pixel.y) * PIXEL_SIZE + p.decimal.y;

     return v;
}

Coord_t pixel_to_coord(Pixel_t p)
{
     Coord_t c;
     c.x = p.x / TILE_SIZE_IN_PIXELS;
     c.y = p.y / TILE_SIZE_IN_PIXELS;
     return c;
}

Coord_t pos_to_coord(Position_t p)
{
     assert(p.decimal.x >= 0.0f && p.decimal.y >= 0.0f);
     return pixel_to_coord(p.pixel);
}

Pixel_t coord_to_pixel(Coord_t c)
{
     Pixel_t p;
     p.x = c.x * TILE_SIZE_IN_PIXELS;
     p.y = c.y * TILE_SIZE_IN_PIXELS;
     return p;
}

Position_t coord_to_pos_at_tile_center(Coord_t c)
{
     return pixel_pos(coord_to_pixel(c) + Pixel_t{HALF_TILE_SIZE_IN_PIXELS, HALF_TILE_SIZE_IN_PIXELS});
}

Position_t coord_to_pos(Coord_t c)
{
     return pixel_pos(coord_to_pixel(c));
}

struct Rect_t{
     S16 left;
     S16 bottom;

     S16 right;
     S16 top;
};

Rect_t pixel_range(Pixel_t bottom_left, Pixel_t top_right){
     Rect_t r {bottom_left.x, bottom_left.y, top_right.x, top_right.y};
     return r;
}

bool pixel_in_rect(Pixel_t p, Rect_t r){
     return (p.x > r.left && p.x < r.right &&
             p.y > r.bottom && p.y < r.top);
}

Rect_t coord_range(Coord_t bottom_left, Coord_t top_right){
     Rect_t r {bottom_left.x, bottom_left.y, top_right.x, top_right.y};
     return r;
}

bool coord_in_rect(Coord_t c, Rect_t r){
     return (c.x >= r.left && c.x <= r.right &&
             c.y >= r.bottom && c.y <= r.top);
}

bool rect_in_rect(Rect_t a, Rect_t b){
     Pixel_t top_left {a.left, a.top};
     Pixel_t top_right {a.right, a.top};
     Pixel_t bottom_left {a.left, a.bottom};
     Pixel_t bottom_right {a.right, a.bottom};
     Pixel_t center {(S16)(a.left + (a.right - a.left) / 2),
                     (S16)(a.bottom + (a.top - a.bottom) / 2),};

     if(pixel_in_rect(bottom_left, b)) return true;
     if(pixel_in_rect(top_left, b)) return true;
     if(pixel_in_rect(bottom_right, b)) return true;
     if(pixel_in_rect(top_right, b)) return true;
     if(pixel_in_rect(center, b)) return true;

     // special case if they line up, are they sliding into each other
     if(a.left == b.left){
          if(a.bottom > b.bottom && a.bottom < b.top){
               return true;
          }else if(a.top > b.bottom && a.top < b.top){
               return true;
          }
     }else if(a.top == b.top){
          if(a.left > b.left && a.left < b.right){
               return true;
          }else if(a.right > b.left && a.right < b.right){
               return true;
          }
     }

     return false;
}

enum TileFlag_t{
     TILE_FLAG_ICED = 1,
     TILE_FLAG_CHECKPOINT = 2,
     TILE_FLAG_WIRE_LEFT = 4,
     TILE_FLAG_WIRE_UP = 8,
     TILE_FLAG_WIRE_RIGHT = 16,
     TILE_FLAG_WIRE_DOWN = 32,
};

struct Tile_t{
     U8 id;
     U8 light;
     U8 flags;
};

struct TileMap_t{
     S16 width;
     S16 height;
     Tile_t** tiles;
};

bool init(TileMap_t* tilemap, S16 width, S16 height){
     tilemap->tiles = (Tile_t**)calloc(height, sizeof(*tilemap->tiles));
     if(!tilemap->tiles) return false;

     for(S16 i = 0; i < height; i++){
          tilemap->tiles[i] = (Tile_t*)calloc(width, sizeof(*tilemap->tiles[i]));
          if(!tilemap->tiles[i]) return false;
     }

     tilemap->width = width;
     tilemap->height = height;

     return true;
}

void destroy(TileMap_t* tilemap){
     for(S16 i = 0; i < tilemap->height; i++){
          free(tilemap->tiles[i]);
     }

     free(tilemap->tiles);
     memset(tilemap, 0, sizeof(*tilemap));
}

enum Element_t : U8{
     ELEMENT_NONE,
     ELEMENT_FIRE,
     ELEMENT_ICE,
     ELEMENT_ONLY_ICED,
     ELEMENT_COUNT
};

Element_t transition_element(Element_t a, Element_t b)
{
     Element_t e = ELEMENT_NONE;

     switch(a){
     default:
          break;
     case ELEMENT_NONE:
          switch(b){
          default:
          case ELEMENT_NONE:
               break;
          case ELEMENT_FIRE:
               e = ELEMENT_FIRE;
               break;
          case ELEMENT_ICE:
               e = ELEMENT_ICE;
               break;
          }
          break;
     case ELEMENT_FIRE:
          switch(b){
          default:
          case ELEMENT_NONE:
          case ELEMENT_FIRE:
               e = ELEMENT_FIRE;
               break;
          case ELEMENT_ICE:
               e = ELEMENT_NONE;
               break;
          }
          break;
     case ELEMENT_ICE:
          switch(b){
          default:
          case ELEMENT_NONE:
          case ELEMENT_ICE:
               e = ELEMENT_ICE;
               break;
          case ELEMENT_FIRE:
               e = ELEMENT_NONE;
               break;
          }
          break;
     case ELEMENT_ONLY_ICED:
          switch(b){
          default:
          case ELEMENT_NONE:
               break;
          case ELEMENT_ICE:
               e = ELEMENT_ICE;
               break;
          case ELEMENT_FIRE:
               e = ELEMENT_FIRE;
               break;
          }
          break;
     }

     return e;
}

Element_t next_element(Element_t e)
{
     int i = (int)(e) + 1;
     return (Element_t)( i % ELEMENT_COUNT );
}

const char* element_to_string(Element_t e)
{
     switch(e){
     default:
          break;
     CASE_ENUM_RET_STR(ELEMENT_NONE);
     CASE_ENUM_RET_STR(ELEMENT_FIRE);
     CASE_ENUM_RET_STR(ELEMENT_ICE);
     CASE_ENUM_RET_STR(ELEMENT_COUNT);
     }

     return "ELEMENT_UNKNOWN";
}

struct Block_t{
     Position_t pos;
     DirectionMask_t force;
     Element_t element;
};

Vec_t collide_circle_with_line(Vec_t circle_center, F32 circle_radius, Vec_t a, Vec_t b){
     // move data we care about to the origin
     Vec_t c = circle_center - a;
     Vec_t l = b - a;
     Vec_t closest_point_on_l_to_c = vec_project_onto(c, l);
     Vec_t collision_to_c = closest_point_on_l_to_c - c;

     if(vec_magnitude(collision_to_c) < circle_radius){
          // find edge of circle in that direction
          Vec_t edge_of_circle = vec_normalize(collision_to_c) * circle_radius;
          return collision_to_c - edge_of_circle;
     }

     return vec_zero();
}

using namespace std::chrono;

int main(){
     if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0){
          return 1;
     }

     SDL_DisplayMode display_mode;

     if(SDL_GetCurrentDisplayMode(0, &display_mode) != 0){
          return 1;
     }

     int window_width = 1280;
     int window_height = 1024;

     SDL_Window* window = SDL_CreateWindow("bryte", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_OPENGL);
     if(!window) return 1;

     SDL_GLContext opengl_context = SDL_GL_CreateContext(window);

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

     Rect_t rooms[2];

     rooms[0].left = 0;
     rooms[0].bottom = 0;
     rooms[0].top = 16;
     rooms[0].right = 16;

     rooms[1].left = 17;
     rooms[1].bottom = 5;
     rooms[1].top = 15;
     rooms[1].right = 27;

     const int block_count = 2;
     Block_t blocks[block_count];

     blocks[0].pos = coord_to_pos(Coord_t{6, 6});
     blocks[1].pos = coord_to_pos(Coord_t{6, 2});

     TileMap_t tilemap;
     {
          init(&tilemap, 34, 17);

          for(U16 i = 0; i < 17; i++){
               tilemap.tiles[0][i].id = 1;
               tilemap.tiles[tilemap.height - 1][i].id = 1;
          }

          for(U16 i = 0; i < 10; i++){
               tilemap.tiles[5][17 + i].id = 1;
               tilemap.tiles[15][17 + i].id = 1;
          }

          for(U16 i = 0; i < tilemap.height; i++){
               tilemap.tiles[i][0].id = 1;
               tilemap.tiles[i][16].id = 1;
               tilemap.tiles[i][17].id = 1;
          }

          for(U16 i = 0; i < 10; i++){
               tilemap.tiles[5 + i][27].id = 1;
          }

          tilemap.tiles[10][17].id = 0;
          tilemap.tiles[10][16].id = 0;

          tilemap.tiles[3][4].id = 1;
          tilemap.tiles[4][5].id = 1;

          tilemap.tiles[8][4].id = 1;
          tilemap.tiles[8][5].id = 1;

          tilemap.tiles[11][10].id = 1;
          tilemap.tiles[12][10].id = 1;

          tilemap.tiles[5][10].id = 1;
          tilemap.tiles[7][10].id = 1;

          tilemap.tiles[5][12].id = 1;
          tilemap.tiles[7][12].id = 1;

          tilemap.tiles[2][14].id = 1;

          tilemap.tiles[8][22].id = 1;
          tilemap.tiles[9][23].id = 1;
     }

     bool quit = false;

     Vec_t user_movement = {};

     Position_t pos = coord_to_pos(Coord_t{3, 3});
     Vec_t vel = vec_zero();
     Vec_t accel = vec_zero();

     bool left_pressed = false;
     bool right_pressed = false;
     bool up_pressed = false;
     bool down_pressed = false;

     auto last_time = system_clock::now();
     auto current_time = last_time;

     Position_t camera {};

     F32 player_radius = 8.0f / 272.0f;

     while(!quit){
          current_time = system_clock::now();
          duration<double> elapsed_seconds = current_time - last_time;
          F64 dt = (F64)(elapsed_seconds.count());

          if(dt < 0.0166666f) continue;

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
                         left_pressed = true;
                         break;
                    case SDL_SCANCODE_RIGHT:
                         right_pressed = true;
                         break;
                    case SDL_SCANCODE_UP:
                         up_pressed = true;
                         break;
                    case SDL_SCANCODE_DOWN:
                         down_pressed = true;
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
                         left_pressed = false;
                         break;
                    case SDL_SCANCODE_RIGHT:
                         right_pressed = false;
                         break;
                    case SDL_SCANCODE_UP:
                         up_pressed = false;
                         break;
                    case SDL_SCANCODE_DOWN:
                         down_pressed = false;
                         break;
                    }
                    break;
               }
          }

          user_movement = vec_zero();

          if(left_pressed) user_movement += Vec_t{-1, 0};
          if(right_pressed) user_movement += Vec_t{1, 0};
          if(up_pressed) user_movement += Vec_t{0, 1};
          if(down_pressed) user_movement += Vec_t{0, -1};

          last_time = current_time;

          // figure out what room we should focus on
          Vec_t pos_vec = pos_to_vec(pos);
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

          Position_t camera_movement = room_center - camera;
          camera += camera_movement * 0.05f;

          // player movement
          {
               float movement_speed = 9.5f;
               float drag = 0.7f;

               user_movement = vec_normalize(user_movement);
               accel = user_movement * movement_speed;

               Vec_t pos_delta = (accel * dt * dt * 0.5f) + (vel * dt);
               vel += accel * dt;
               vel *= drag;

               if(fabs(vec_magnitude(vel)) > movement_speed){
                    vel = vec_normalize(vel) * movement_speed;
               }

               // figure out tiles that are close by
               Coord_t coord = pos_to_coord(pos);
               Coord_t min = coord - Coord_t{1, 1};
               Coord_t max = coord + Coord_t{1, 1};
               min = coord_clamp_zero_to_dim(min, tilemap.width - 1, tilemap.height - 1);
               max = coord_clamp_zero_to_dim(max, tilemap.width - 1, tilemap.height - 1);

               for(S16 y = min.y; y <= max.y; y++){
                    for(S16 x = min.x; x <= max.x; x++){
                         if(tilemap.tiles[y][x].id){
                              Position_t relative = coord_to_pos(Coord_t{x, y}) - pos;
                              Vec_t bottom_left = pos_to_vec(relative);
                              Vec_t top_left {bottom_left.x, bottom_left.y + TILE_SIZE};
                              Vec_t top_right {bottom_left.x + TILE_SIZE, bottom_left.y + TILE_SIZE};
                              Vec_t bottom_right {bottom_left.x + TILE_SIZE, bottom_left.y};

                              pos_delta += collide_circle_with_line(pos_delta, player_radius, bottom_left, top_left);
                              pos_delta += collide_circle_with_line(pos_delta, player_radius, top_left, top_right);
                              pos_delta += collide_circle_with_line(pos_delta, player_radius, bottom_right, top_right);
                              pos_delta += collide_circle_with_line(pos_delta, player_radius, bottom_left, bottom_right);
                         }
                    }
               }

               for(U16 i = 0; i < block_count; i++){
                    Position_t relative = blocks[i].pos - pos;
                    Vec_t bottom_left = pos_to_vec(relative);
                    if(vec_magnitude(bottom_left) > (2 * TILE_SIZE)) continue;

                    Vec_t top_left {bottom_left.x, bottom_left.y + TILE_SIZE};
                    Vec_t top_right {bottom_left.x + TILE_SIZE, bottom_left.y + TILE_SIZE};
                    Vec_t bottom_right {bottom_left.x + TILE_SIZE, bottom_left.y};

                    pos_delta += collide_circle_with_line(pos_delta, player_radius, bottom_left, top_left);
                    pos_delta += collide_circle_with_line(pos_delta, player_radius, top_left, top_right);
                    pos_delta += collide_circle_with_line(pos_delta, player_radius, bottom_right, top_right);
                    pos_delta += collide_circle_with_line(pos_delta, player_radius, bottom_left, bottom_right);
               }

               pos += pos_delta;
          }

          glClear(GL_COLOR_BUFFER_BIT);

          // player circle
          glBegin(GL_TRIANGLE_FAN);
          glColor3f(1.0f, 1.0f, 1.0f);

          Position_t screen_camera = camera - Vec_t{0.5f, 0.5f} + Vec_t{HALF_TILE_SIZE, HALF_TILE_SIZE};
          Position_t player_camera_offset = pos - screen_camera;
          pos_vec = pos_to_vec(player_camera_offset);

          glVertex2f(pos_vec.x, pos_vec.y);
          glVertex2f(pos_vec.x + player_radius, pos_vec.y);
          S32 segments = 32;
          F32 delta = 3.14159f * 2.0f / (F32)(segments);
          F32 angle = 0.0f  + delta;
          for(S32 i = 0; i <= segments; i++){
               F32 dx = cos(angle) * player_radius;
               F32 dy = sin(angle) * player_radius;

               glVertex2f(pos_vec.x + dx, pos_vec.y + dy);
               angle += delta;
          }
          glVertex2f(pos_vec.x + player_radius, pos_vec.y);
          glEnd();

          // tilemap
          Coord_t min = pos_to_coord(screen_camera);
          Coord_t max = min + Coord_t{ROOM_TILE_SIZE, ROOM_TILE_SIZE};
          min = coord_clamp_zero_to_dim(min, tilemap.width - 1, tilemap.height - 1);
          max = coord_clamp_zero_to_dim(max, tilemap.width - 1, tilemap.height - 1);
          Position_t tile_bottom_left = coord_to_pos(min);
          Vec_t camera_offset = pos_to_vec(tile_bottom_left - screen_camera);
          glBegin(GL_QUADS);
          glColor3f(0.0f, 1.0f, 1.0f);
          for(U16 y = min.y; y <= max.y; y++){
               for(U16 x = min.x; x <= max.x; x++){
                    if(tilemap.tiles[y][x].id){
                         Vec_t tile_pos {(F32)(x - min.x) * TILE_SIZE + camera_offset.x, (F32)(y - min.y) * TILE_SIZE + camera_offset.y};
                         glVertex2f(tile_pos.x, tile_pos.y);
                         glVertex2f(tile_pos.x, tile_pos.y + TILE_SIZE);
                         glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y + TILE_SIZE);
                         glVertex2f(tile_pos.x + TILE_SIZE, tile_pos.y);
                    }
               }
          }
          glEnd();

          // block
          glBegin(GL_QUADS);
          glColor3f(1.0f, 1.0f, 0.0f);
          for(U16 i = 0; i < block_count; i++){
               Position_t block_camera_offset = blocks[i].pos - screen_camera;
               pos_vec = pos_to_vec(block_camera_offset);
               glVertex2f(pos_vec.x, pos_vec.y);
               glVertex2f(pos_vec.x, pos_vec.y + TILE_SIZE);
               glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y + TILE_SIZE);
               glVertex2f(pos_vec.x + TILE_SIZE, pos_vec.y);
          }
          glEnd();

          SDL_GL_SwapWindow(window);
     }

     destroy(&tilemap);

     SDL_GL_DeleteContext(opengl_context);
     SDL_DestroyWindow(window);
     SDL_Quit();

     return 0;
}
