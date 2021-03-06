#include "rect.h"
#include "defines.h"

#include <math.h>
#include <stdlib.h>

bool xy_in_rect(const Rect_t& rect, S16 x, S16 y){
     return (x >= rect.left && x <= rect.right &&
             y >= rect.bottom && y <= rect.top);
}

bool pixel_in_rect(Pixel_t p, Rect_t r){
     return xy_in_rect(r, p.x, p.y);
}

bool coord_in_rect(Coord_t c, Rect_t r){
     return xy_in_rect(r, c.x, c.y);
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

     // another special case where they are like this:
     //            __
     //           |  |
     //      _____|  |____
     //      |    |  |    |
     //      |____|  |____|
     //           |  |
     //            __
     //
     //
     if(a.left >= b.left && a.right <= b.right &&
        a.bottom <= b.bottom && a.top >= b.top){
          return true;
     }

     return false;
}

bool axis_line_intersects_rect(AxisLine_t l, Rect_t r){
     AxisLine_t top {false, r.top, r.left, r.right};
     AxisLine_t bottom {false, r.bottom, r.left, r.right};
     AxisLine_t left {true, r.left, r.bottom, r.top};
     AxisLine_t right {true, r.right, r.bottom, r.top};

     if(axis_lines_intersect(l, top)) return true;
     if(axis_lines_intersect(l, bottom)) return true;
     if(axis_lines_intersect(l, left)) return true;
     if(axis_lines_intersect(l, right)) return true;

     return false;
}

S16 rect_area(Rect_t a){
     return (a.right - a.left) * (a.top - a.bottom);
}

S16 rect_intersecting_area(Rect_t a, Rect_t b){
     auto horizontal = MINIMUM(a.right, b.right) - MAXIMUM(a.left, b.left);
     auto vertical = MINIMUM(a.top, b.top) - MAXIMUM(a.bottom, b.bottom);
     return abs(horizontal * vertical);
}

bool rect_completely_in_rect(Rect_t a, Rect_t b){
    return (a.left >= b.left && a.left <= b.right &&
            a.right >= b.left && a.right <= b.right &&
            a.bottom >= b.bottom && a.bottom <= b.top &&
            a.top >= b.bottom && a.top <= b.top);
}
