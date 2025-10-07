#include <float.h>
#include "shape.hpp"

using namespace std;

namespace picovector {


  void offset_line_segment(point &s, point &e, float offset) {
    // calculate normal of edge
    float nx = -(e.y - s.y);
    float ny = e.x - s.x;
    float l = sqrt(nx * nx + ny * ny);
    nx /= l;
    ny /= l;

    // scale normal to requested offset
    float ox = nx * offset;
    float oy = ny * offset;

    // offset supplied edge points
    s.x += ox;
    s.y += oy;
    e.x += ox;
    e.y += oy;
  }

  bool intersection(point p1, point p2, point p3, point p4, point &i) {
    float a1 = p2.y - p1.y;
    float b1 = p1.x - p2.x;
    float c1 = a1 * p1.x + b1 * p1.y;

    float a2 = p4.y - p3.y;
    float b2 = p3.x - p4.x;
    float c2 = a2 * p3.x + b2 * p3.y;

    float determinant = a1 * b2 - a2 * b1;

    if(determinant == 0) {
      return false; // lines parallel or coincident
    }

    i.x = (b2 * c1 - b1 * c2) / determinant;
    i.y = (a1 * c2 - a2 * c1) / determinant;
    return true;
  }



  shape::shape(int path_count) {
    //debug_printf("shape constructed\n");
    paths.reserve(path_count);
  }

  void shape::add_path(path path) {
    paths.push_back(path);
  }

  rect shape::bounds() {
    float minx = FLT_MAX, miny = FLT_MAX, maxx = -FLT_MAX, maxy = -FLT_MAX;
    for(const path &path : paths) {
      for(point point : path.points) {
        point = point.transform(&transform);
        minx = min(minx, point.x);
        miny = min(miny, point.y);
        maxx = max(maxx, point.x);
        maxy = max(maxy, point.y);
      }
    }
    return rect(minx, miny, ceil(maxx) - minx, ceil(maxy) - miny);
  }

  // these should be methods on image maybe?
  // void shape::draw(image &img) {
  //   if(style) {
  //     render(*this, img, style);
  //   }
  // }

  void shape::stroke(float thickness) {
    for(int i = 0; i < (int)this->paths.size(); i++) {
      this->paths[i].stroke(thickness);
    }
  }



  path::path(int point_count) {
    points.reserve(point_count);
  }

  void path::add_point(const point &point) {
    points.push_back(point);
  }

  void path::add_point(float x, float y) {
    points.push_back(point(x, y));
  }

  void path::edge_points(int edge, point &s, point &e) {
    // return the two points that make up an edge
    s = edge == -1 ? points.back() : points[edge];
    e = edge == (int)points.size() - 1 ? points.front() : points[edge + 1];
  }

  void path::stroke(float offset) {
    int c = points.size();
    vector<point, PV_STD_ALLOCATOR<point>> new_points(c);

    if(c == 2) {
        point p1, p2; // edge 1 start and end
        edge_points(0, p1, p2);
        offset_line_segment(p1, p2, offset);
        points.push_back(p2);
        points.push_back(p1);
    }else{
      for(int i = 0; i < c; i++) {
        point p1, p2; // edge 1 start and end
        edge_points(i - 1, p1, p2);
        offset_line_segment(p1, p2, offset);

        point p3, p4; // edge 2 start and end
        edge_points(i, p3, p4);
        offset_line_segment(p3, p4, offset);

        // find intersection of the edges
        point pi;
        bool ok = intersection(p1, p2, p3, p4, pi);
        new_points[i] = pi;
      }

      points.push_back(points.front());
      points.insert(points.end(), new_points.begin(), new_points.end());
      points.push_back(new_points.front());
    }
  }

  void path::inflate(float offset) {
    vector<point, PV_STD_ALLOCATOR<point>> new_points(points.size());

    int edge_count = points.size();
    for(int i = 0; i < edge_count; i++) {
      point p1, p2; // edge 1 start and end
      edge_points(i, p1, p2);
      offset_line_segment(p1, p2, offset);

      point p3, p4; // edge 2 start and end
      edge_points(i + 1, p3, p4);
      offset_line_segment(p3, p4, offset);

      // find intersection of the edges
      point pi;
      bool ok = intersection(p1, p2, p3, p4, pi);
      new_points[i] = pi;
    }

    points = new_points;
  }

}
