/**
*    Copyright (C) 2008-2012 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*    As a special exception, the copyright holders give permission to link the
*    code of portions of this program with the OpenSSL library under certain
*    conditions as described in each individual source file and distribute
*    linked combinations including the program with the OpenSSL library. You
*    must comply with the GNU Affero General Public License in all respects for
*    all of the code used other than as permitted herein. If you modify file(s)
*    with this exception, you may extend this exception to your version of the
*    file(s), but you are not obligated to do so. If you do not wish to do so,
*    delete this exception statement from your version. If you delete this
*    exception statement from all source files in the program, then also delete
*    it in the license file.
*/

#include "mongo/pch.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/geo/core.h"
#include "mongo/db/geo/shapes.h"
#include "mongo/util/mongoutils/str.h"

// So we can get at the str namespace.
using namespace mongoutils;

namespace mongo {

////////////// Point

    Point::Point() : x(0), y(0) { }

    Point::Point(double x, double y) : x(x), y(y) { }

    Point::Point(const BSONElement& e) {
        BSONObjIterator i(e.Obj());
        x = i.next().number();
        y = i.next().number();
    }

    Point::Point(const BSONObj& o) {
        BSONObjIterator i(o);
        x = i.next().number();
        y = i.next().number();
    }

    string Point::toString() const {
        StringBuilder buf;
        buf << "(" << x << "," << y << ")";
        return buf.str();
    }

////////////// Circle

    Circle::Circle() {}
    Circle::Circle(double radius, Point center) : radius(radius), center(center) {}


////////////// Box

    Box::Box() {}

    Box::Box(double x, double y, double size) :
        _min(x, y), _max(x + size, y + size) {
    }

    Box::Box(const Point& ptA, const Point& ptB) {
        init(ptA, ptB);
    }

    void Box::init(const Point& ptA, const Point& ptB) {
        _min.x = min(ptA.x, ptB.x);
        _min.y = min(ptA.y, ptB.y);
        _max.x = max(ptA.x, ptB.x);
        _max.y = max(ptA.y, ptB.y);
    }

    void Box::init(const Box& other) {
        init(other._min, other._max);
    }

    BSONArray Box::toBSON() const {
        return BSON_ARRAY(BSON_ARRAY(_min.x << _min.y) << BSON_ARRAY(_max.x << _max.y));
    }

    string Box::toString() const {
        StringBuilder buf;
        buf << _min.toString() << " -->> " << _max.toString();
        return buf.str();
    }

    bool Box::between(double min, double max, double val, double fudge) const {
        return val + fudge >= min && val <= max + fudge;
    }

    bool Box::onBoundary(double bound, double val, double fudge) const {
        return (val >= bound - fudge && val <= bound + fudge);
    }

    bool Box::mid(double amin, double amax,
                  double bmin, double bmax, bool min, double* res) const {
        verify(amin <= amax);
        verify(bmin <= bmax);

        if (amin < bmin) {
            if (amax < bmin)
                return false;
            *res = min ? bmin : amax;
            return true;
        }
        if (amin > bmax)
            return false;
        *res = min ? amin : bmax;
        return true;
    }

    bool Box::intersects(const Box& other) const {

        bool intersectX = between(_min.x, _max.x, other._min.x) // contain part of other range
                          || between(_min.x, _max.x, other._max.x) // contain part of other range
                          || between(other._min.x, other._max.x, _min.x); // other range contains us

        bool intersectY = between(_min.y, _max.y, other._min.y)
                          || between(_min.y, _max.y, other._max.y)
                          || between(other._min.y, other._max.y, _min.y);

        return intersectX && intersectY;
    }

    double Box::legacyIntersectFraction(const Box& other) const {

        Point boundMin(0,0);
        Point boundMax(0,0);

        if (!mid(_min.x, _max.x, other._min.x, other._max.x, true, &boundMin.x) ||
            !mid(_min.x, _max.x, other._min.x, other._max.x, false, &boundMax.x) ||
            !mid(_min.y, _max.y, other._min.y, other._max.y, true, &boundMin.y) ||
            !mid(_min.y, _max.y, other._min.y, other._max.y, false, &boundMax.y))
            return 0;

        Box intersection(boundMin, boundMax);
        return intersection.area() / area();
    }

    double Box::area() const {
        return (_max.x - _min.x) * (_max.y - _min.y);
    }

    double Box::maxDim() const {
        return max(_max.x - _min.x, _max.y - _min.y);
    }

    Point Box::center() const {
        return Point((_min.x + _max.x) / 2,
                     (_min.y + _max.y) / 2);
    }

    void Box::truncate(double min, double max) {
        if (_min.x < min) _min.x = min;
        if (_min.y < min) _min.y = min;
        if (_max.x > max) _max.x = max;
        if (_max.y > max) _max.y = max;
    }

    void Box::fudge(double error) {
        _min.x -= error;
        _min.y -= error;
        _max.x += error;
        _max.y += error;
    }

    void Box::expandToInclude(const Point& pt) {
        _min.x = min(_min.x, pt.x);
        _min.y = min(_min.y, pt.y);
        _max.x = max(_max.x, pt.x);
        _max.y = max(_max.y, pt.y);
    }

    bool Box::onBoundary(Point p, double fudge) const {
        return onBoundary(_min.x, p.x, fudge) ||
               onBoundary(_max.x, p.x, fudge) ||
               onBoundary(_min.y, p.y, fudge) ||
               onBoundary(_max.y, p.y, fudge);
    }

    bool Box::inside(Point p, double fudge) const {
        bool res = inside(p.x, p.y, fudge);
        return res;
    }

    bool Box::inside(double x, double y, double fudge) const {
        return between(_min.x, _max.x , x, fudge) &&
               between(_min.y, _max.y , y, fudge);
    }

    bool Box::contains(const Box& other, double fudge) const {
        return inside(other._min, fudge) && inside(other._max, fudge);
    }

////////////// Polygon

    Polygon::Polygon() {
    }

    Polygon::Polygon(const vector<Point>& points) {
        init(points);
    }

    void Polygon::init(const vector<Point>& points) {

        _points.clear();
        _bounds.reset();
        _centroid.reset();

        _points.insert(_points.begin(), points.begin(), points.end());
    }

    void Polygon::init(const Polygon& other) {
        init(other._points);
    }

    int Polygon::size(void) const { return _points.size(); }

    bool Polygon::contains(const Point& p) const { return contains(p, 0) > 0; }

    /* 
     * Return values:
     * -1 if no intersection
     * 0 if maybe an intersection (using fudge)
     * 1 if there is an intersection
     *
     * A ray casting intersection method is used.
     */
    int Polygon::contains(const Point &p, double fudge) const {
        Box fudgeBox(Point(p.x - fudge, p.y - fudge), Point(p.x + fudge, p.y + fudge));

        int counter = 0;
        Point p1 = _points[0];
        for (int i = 1; i <= size(); i++) {
            // XXX: why is there a mod here?
            Point p2 = _points[i % size()];

            // We need to check whether or not this segment intersects our error box
            if (fudge > 0 &&
                    // Points not too far below box
                    fudgeBox._min.y <= std::max(p1.y, p2.y) &&
                    // Points not too far above box
                    fudgeBox._max.y >= std::min(p1.y, p2.y) &&
                    // Points not too far to left of box
                    fudgeBox._min.x <= std::max(p1.x, p2.x) &&
                    // Points not too far to right of box
                    fudgeBox._max.x >= std::min(p1.x, p2.x)) {


                // If our box contains one or more of these points, we need to do an exact
                // check.
                if (fudgeBox.inside(p1)) {
                    return 0;
                }
                if (fudgeBox.inside(p2)) {
                    return 0;
                }

                // Do intersection check for vertical sides
                if (p1.y != p2.y) {
                    double invSlope = (p2.x - p1.x) / (p2.y - p1.y);

                    double xintersT = (fudgeBox._max.y - p1.y) * invSlope + p1.x;
                    if (fudgeBox._min.x <= xintersT && fudgeBox._max.x >= xintersT) {
                        return 0;
                    }

                    double xintersB = (fudgeBox._min.y - p1.y) * invSlope + p1.x;
                    if (fudgeBox._min.x <= xintersB && fudgeBox._max.x >= xintersB) {
                        return 0;
                    }
                }

                // Do intersection check for horizontal sides
                if (p1.x != p2.x) {
                    double slope = (p2.y - p1.y) / (p2.x - p1.x);

                    double yintersR = (p1.x - fudgeBox._max.x) * slope + p1.y;
                    if (fudgeBox._min.y <= yintersR && fudgeBox._max.y >= yintersR) {
                        return 0;
                    }

                    double yintersL = (p1.x - fudgeBox._min.x) * slope + p1.y;
                    if (fudgeBox._min.y <= yintersL && fudgeBox._max.y >= yintersL) {
                        return 0;
                    }
                }
            } else if (fudge == 0){
                // If this is an exact vertex, we won't intersect, so check this
                if (p.y == p1.y && p.x == p1.x) return 1;
                else if (p.y == p2.y && p.x == p2.x) return 1;

                // If this is a horizontal line we won't intersect, so check this
                if (p1.y == p2.y && p.y == p1.y){
                    // Check that the x-coord lies in the line
                    if (p.x >= std::min(p1.x, p2.x) && p.x <= std::max(p1.x, p2.x))
                        return 1;
                }
            }

            // Normal intersection test.
            // TODO: Invert these for clearer logic?
            if (p.y > std::min(p1.y, p2.y)) {
                if (p.y <= std::max(p1.y, p2.y)) {
                    if (p.x <= std::max(p1.x, p2.x)) {
                        if (p1.y != p2.y) {
                            double xinters = (p.y-p1.y)*(p2.x-p1.x)/(p2.y-p1.y)+p1.x;
                            // Special case of point on vertical line
                            if (p1.x == p2.x && p.x == p1.x){

                                // Need special case for the vertical edges, for example:
                                // 1) \e   pe/----->
                                // vs.
                                // 2) \ep---e/----->
                                //
                                // if we count exact as intersection, then 1 is in but 2 is out
                                // if we count exact as no-int then 1 is out but 2 is in.

                                return 1;
                            } else if (p1.x == p2.x || p.x <= xinters) {
                                counter++;
                            }
                        }
                    }
                }
            }

            p1 = p2;
        }

        if (counter % 2 == 0) {
            return -1;
        } else {
            return 1;
        }
    }

    const Point& Polygon::centroid() const {

        if (_centroid) {
            return *_centroid;
        }

        _centroid.reset(new Point());

        double signedArea = 0.0;
        double area = 0.0;  // Partial signed area

        /// For all vertices except last
        int i = 0;
        for (i = 0; i < size() - 1; ++i) {
            area = _points[i].x * _points[i+1].y - _points[i+1].x * _points[i].y ;
            signedArea += area;
            _centroid->x += (_points[i].x + _points[i+1].x) * area;
            _centroid->y += (_points[i].y + _points[i+1].y) * area;
        }

        // Do last vertex
        area = _points[i].x * _points[0].y - _points[0].x * _points[i].y;
        _centroid->x += (_points[i].x + _points[0].x) * area;
        _centroid->y += (_points[i].y + _points[0].y) * area;
        signedArea += area;
        signedArea *= 0.5;
        _centroid->x /= (6 * signedArea);
        _centroid->y /= (6 * signedArea);

        return *_centroid;
    }

    const Box& Polygon::bounds() const {

        if (_bounds) {
            return *_bounds;
        }

        _bounds.reset(new Box(_points[0], _points[0]));

        for (int i = 1; i < size(); i++) {
            _bounds->expandToInclude(_points[i]);
        }

        return *_bounds;
    }

    R2Annulus::R2Annulus() :
        _inner(0.0), _outer(0.0) {
    }

    R2Annulus::R2Annulus(const Point& center, double inner, double outer) :
        _center(center), _inner(inner), _outer(outer) {
    }

    const Point& R2Annulus::center() const {
        return _center;
    }

    double R2Annulus::getInner() const {
        return _inner;
    }

    double R2Annulus::getOuter() const {
        return _outer;
    }

    bool R2Annulus::contains(const Point& point, double maxError) const {

        // See if we're inside the inner radius
        if (distanceWithin(point, _center, getInner() - maxError)) {
            return false;
        }

        // See if we're outside the outer radius
        if (!distanceWithin(point, _center, getOuter() + maxError)) {
            return false;
        }

        return true;
    }

    /////// Other methods

    /**
     * Distance method that compares x or y coords when other direction is zero,
     * avoids numerical error when distances are very close to radius but axis-aligned.
     *
     * An example of the problem is:
     * (52.0 - 51.9999) - 0.0001 = 3.31965e-15 and 52.0 - 51.9999 > 0.0001 in double arithmetic
     * but:
     * 51.9999 + 0.0001 <= 52.0
     *
     * This avoids some (but not all!) suprising results in $center queries where points are
     * (radius + center.x, center.y) or vice-versa.
     */
    bool distanceWithin(const Point &p1, const Point &p2, double radius) {
        double a = p2.x - p1.x;
        double b = p2.y - p1.y;

        if (a == 0) {
            //
            // Note:  For some, unknown reason, when a 32-bit g++ optimizes this call, the sum is
            // calculated imprecisely.  We need to force the compiler to always evaluate it
            // correctly, hence the weirdness.
            //
            // On some 32-bit linux machines, removing the volatile keyword or calculating the sum
            // inline will make certain geo tests fail.  Of course this check will force volatile
            // for all 32-bit systems, not just affected systems.
            if (sizeof(void*) <= 4){
                volatile double sum = p2.y > p1.y ? p1.y + radius : p2.y + radius;
                return p2.y > p1.y ? sum >= p2.y : sum >= p1.y;
            } else {
                // Original math, correct for most systems
                return p2.y > p1.y ? p1.y + radius >= p2.y : p2.y + radius >= p1.y;
            }
        }

        if (b == 0) {
            if (sizeof(void*) <= 4){
                volatile double sum = p2.x > p1.x ? p1.x + radius : p2.x + radius;
                return p2.x > p1.x ? sum >= p2.x : sum >= p1.x;
            } else {
                return p2.x > p1.x ? p1.x + radius >= p2.x : p2.x + radius >= p1.x;
            }
        }

        return sqrt((a * a) + (b * b)) <= radius;
    }

    // Technically lat/long bounds, not really tied to earth radius.
    void checkEarthBounds(const Point &p) {
        uassert(14808, str::stream() << "point " << p.toString()
                                     << " must be in earth-like bounds of long "
                                     << ": [-180, 180], lat : [-90, 90] ",
                p.x >= -180 && p.x <= 180 && p.y >= -90 && p.y <= 90);
    }


    // WARNING: x and y MUST be longitude and latitude in that order
    // note: multiply by earth radius for distance
    double spheredist_rad(const Point& p1, const Point& p2) {
        // this uses the n-vector formula: http://en.wikipedia.org/wiki/N-vector
        // If you try to match the code to the formula, note that I inline the cross-product.

        double sinx1(sin(p1.x)), cosx1(cos(p1.x));
        double siny1(sin(p1.y)), cosy1(cos(p1.y));
        double sinx2(sin(p2.x)), cosx2(cos(p2.x));
        double siny2(sin(p2.y)), cosy2(cos(p2.y));

        double cross_prod =
            (cosy1*cosx1 * cosy2*cosx2) +
            (cosy1*sinx1 * cosy2*sinx2) +
            (siny1        * siny2);

        if (cross_prod >= 1 || cross_prod <= -1) {
            // fun with floats
            verify(fabs(cross_prod)-1 < 1e-6);
            return cross_prod > 0 ? 0 : M_PI;
        }

        return acos(cross_prod);
    }

    // @param p1 A point on the sphere where x and y are degrees.
    // @param p2 A point on the sphere where x and y are degrees.
    // @return The distance between the two points in RADIANS.  Multiply by radius to get arc
    // length.
    double spheredist_deg(const Point& p1, const Point& p2) {
        return spheredist_rad(Point(deg2rad(p1.x), deg2rad(p1.y)),
                              Point(deg2rad(p2.x), deg2rad(p2.y)));
    }

    double distance(const Point& p1, const Point &p2) {
        double a = p1.x - p2.x;
        double b = p1.y - p2.y;

        // Avoid numerical error if possible...
        if (a == 0) return abs(b);
        if (b == 0) return abs(a);

        return sqrt((a * a) + (b * b));
    }

    static inline Vector2_d toVector2(const Point& p) {
        return Vector2_d(p.x, p.y);
    }

    // Given a segment (A, B) and a segment (C, D), check whether they intersect.
    bool linesIntersect(const Point& pA, const Point& pB, const Point& pC, const Point& pD) {
        Vector2_d a = toVector2(pA);
        Vector2_d b = toVector2(pB);
        Vector2_d c = toVector2(pC);
        Vector2_d d = toVector2(pD);

        // The normal of line AB
        Vector2_d normalAB = (b - a).Ortho();

        // Dot products of AC and the normal of AB
        // = 0 : C is on the line AB
        // > 0 : C is on one side
        // < 0 : C is on the other side
        double dotProdNormalAB_AC = normalAB.DotProd(c - a);
        double dotProdNormalAB_AD = normalAB.DotProd(d - a);

        // C and D can not on the same side of line AB
        if (dotProdNormalAB_AC * dotProdNormalAB_AD > 0) return false;

        // AB and CD are on the same line
        if (dotProdNormalAB_AC == 0 && dotProdNormalAB_AD == 0) {
            // Test if C or D is on segment AB.
            return (c - a).DotProd(c - b) <= 0 || (d - a).DotProd(d - b) <= 0;
        }

        // Check if A and B are on different sides of line CD.
        Vector2_d normalCD = (d - c).Ortho();
        double dotProdNormalCD_CA = normalCD.DotProd(a - c);
        double dotProdNormalCD_CB = normalCD.DotProd(b - c);
        return dotProdNormalCD_CA * dotProdNormalCD_CB <= 0; // Perhaps A or B is on line CD
    }

    // Check the intersection by measuring the distance between circle center and box center.
    bool circleIntersectsWithBox(const Circle& circle, const Box& box) {
        /* Collapses the four quadrants down into one.
         *   ________
         * r|___B___ \  <- a quarter round corner here. Let's name it "D".
         *  |       | |
         * h|       | |
         *  |   A   |C|
         *  |_______|_|
         *      w    r
         */

        Point boxCenter = box.center();
        double dx = abs(circle.center.x - boxCenter.x);
        double dy = abs(circle.center.y - boxCenter.y);
        double w = (box._max.x - box._min.x) / 2;
        double h = (box._max.y - box._min.y) / 2;
        const double& r = circle.radius;

        // Check if circle.center is in A, B or C.
        // The circle center could be above the box (B) or right to the box (C), but close enough.
        if ((dx <= w + r && dy <= h) || (dx <= w && dy <= h + r)) return true;

        // Now check if circle.center is in the round corner "D".
        return distanceWithin(Point(dx, dy), Point(w, h), r);
    }

    bool lineIntersectsWithBox(const Point& a, const Point& b, const Box& box) {
        Point upperLeft(box._min.x, box._max.y);
        Point lowerRight(box._max.x, box._min.y);

        return linesIntersect(a, b, upperLeft, box._min)
                || linesIntersect(a, b, box._min, lowerRight)
                || linesIntersect(a, b, lowerRight, box._max)
                || linesIntersect(a, b, box._max, upperLeft);
    }

    // Doc: The last point specified is always implicitly connected to the first.
    // [[ 0 , 0 ], [ 3 , 6 ], [ 6 , 0 ]]
    bool edgesIntersectsWithBox(const vector<Point>& vertices, const Box& box) {
        for (size_t i = 0; i < vertices.size() - 1; i++) {
            if (lineIntersectsWithBox(vertices[i], vertices[i+1], box)) return true;
        }
        // The last point and first point.
        return lineIntersectsWithBox(vertices[vertices.size() - 1], vertices[0], box);
    }

    bool polygonContainsBox(const Polygon& polygon, const Box& box) {
        // All vertices of box have to be inside the polygon.
        if (!polygon.contains(box._min)
                || !polygon.contains(box._max)
                || !polygon.contains(Point(box._min.x, box._max.y))
                || !polygon.contains(Point(box._max.x, box._min.y)))
            return false;

        // No intersection between the polygon edges and the box.
        return !edgesIntersectsWithBox(polygon.points(), box);
    }

    bool polygonIntersectsWithBox(const Polygon& polygon, const Box& box) {
        // 1. Polygon contains the box.
        // Check the relaxed condition that whether the polygon include any vertex of the box.
        if (polygon.contains(box._min)
                || polygon.contains(box._max)
                || polygon.contains(Point(box._min.x, box._max.y))
                || polygon.contains(Point(box._max.x, box._min.y)))
            return true;

        // 2. Box contains polygon.
        // Check the relaxed condition that whether the box include any vertex of the polygon.
        for (vector<Point>::const_iterator it = polygon.points().begin();
                it != polygon.points().end(); it++) {
            if (box.inside(*it)) return true;
        }

        // 3. Otherwise they intersect on a portion of both shapes.
        // Edges intersects
        return edgesIntersectsWithBox(polygon.points(), box);
    }

}  // namespace mongo
