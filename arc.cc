/*
 * Copyright © 2011  Google, Inc.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Google Author(s): Behdad Esfahbod
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <glib.h>

#include <deque>

#include "geometry.hh"
#include "cairo-helper.hh"


using namespace std;
using namespace Geometry;
using namespace CairoHelper;

#define MAX_ITERS 20
#define EPSILON 1

typedef Vector<Coord> vector_t;
typedef Point<Coord> point_t;
typedef Line<Coord> line_t;
typedef Circle<Coord, Scalar> circle_t;
typedef Arc<Coord, Scalar> arc_t;
typedef Bezier<Coord> bezier_t;


/* This is a fast approximation of max_dev(). */
static double
max_dev_approx (double d0, double d1)
{
  d0 = fabs (d0);
  d1 = fabs (d1);
  double e0 = 3./4. * MAX (d0, d1);
  double e1 = 4./9. * (d0 + d1);
  return MIN (e0, e1);
}

/* Returns max(abs(d₀ t (1-t)² + d₁ t² (1-t)) for 0≤t≤1. */
static double
max_dev (double d0, double d1)
{
  double candidates[4] = {0,1};
  unsigned int num_candidates = 2;
  if (d0 == d1)
    candidates[num_candidates++] = .5;
  else {
    double delta = d0*d0 - d0*d1 + d1*d1;
    double t2 = 1. / (3 * (d0 - d1));
    double t0 = (2 * d0 - d1) * t2;
    if (delta == 0)
      candidates[num_candidates++] = t0;
    else if (delta > 0) {
      /* This code can be optimized to avoid the sqrt if the solution
       * is not feasible (ie. lies outside (0,1)).  I have implemented
       * that in cairo-spline.c:_cairo_spline_bound().  Can be reused
       * here.
       */
      double t1 = sqrt (delta) * t2;
      candidates[num_candidates++] = t0 - t1;
      candidates[num_candidates++] = t0 + t1;
    }
  }

  double e = 0;
  for (unsigned int i = 0; i < num_candidates; i++) {
    double t = candidates[i];
    double ee;
    if (t < 0. || t > 1.)
      continue;
    ee = fabs (3 * t * (1-t) * (d0 * (1 - t) + d1 * t));
    e = MAX (e, ee);
  }

  return e;
}

double bezier_arc_error (const bezier_t &b0,
			 const arc_t &a)
{
  double ea;
  bezier_t b1 = a.approximate_bezier (&ea);

  assert (b0.p0 == b1.p0);
  assert (b0.p3 == b1.p3);

//  return max_dev ((b1.p1 - b0.p1).len (), (b1.p2 - b0.p2).len ());

  vector_t v0 = b1.p1 - b0.p1;
  vector_t v1 = b1.p2 - b0.p2;

  vector_t b = (b0.p3 - b0.p0).normal ();
  v0 = v0.rebase (b);
  v1 = v1.rebase (b);

  vector_t v (max_dev (v0.dx, v1.dx),
	      max_dev (v0.dy, v1.dy));

  vector_t b2 = (b1.p3 - b1.p2).rebase (b).normal ();
  vector_t u = v.rebase (b2);

  Scalar c = (b1.p3 - b1.p0).len ();
  double r = fabs (c * (a.d * a.d + 1) / (4 * a.d));
  double eb = sqrt ((r + u.dx) * (r + u.dx) + u.dy * u.dy) - r;

  return ea + eb;
}


double
arc_bezier_error (const bezier_t &b,
		  const circle_t &c)
{
  point_t p0 = b.p0;
  point_t p1 = b.p1;
  point_t p2 = b.p2;
  point_t p3 = b.p3;
  double a0, a1, a4, _4_3_tan_a4;
  point_t p1s (0,0), p2s (0,0);
  double ea, eb, e;

  a0 = (p0 - c.c).angle ();
  a1 = (p3 - c.c).angle ();
  a4 = (a1 - a0) / 4.;
  _4_3_tan_a4 = 4./3.*tan (a4);
  p1s = p0 + (p0 - c.c).perpendicular () * _4_3_tan_a4;
  p2s = p3 + (c.c - p3).perpendicular () * _4_3_tan_a4;

  ea = 2./27.*c.r*pow(sin(a4),6)/pow(cos(a4)/4.,2);
  //eb = max_dev ((p1s - p1).len (), (p2s - p2).len ());
  {
    vector_t v0 = p1s - p1;
    vector_t v1 = p2s - p2;

    vector_t b = (p0 - c.c + p3 - c.c).normalized ();
    v0 = v0.rebase (b);
    v1 = v1.rebase (b);

    vector_t v (max_dev (v0.dx, v1.dx),
		max_dev (v0.dy, v1.dy));

    vector_t b2 = (p3 - c.c).rebase (b).normalized ();
    vector_t u = v.rebase (b2);

    eb = sqrt ((c.r + u.dx) * (c.r + u.dx) + u.dy * u.dy) - c.r;
  }
  e = ea + eb;

  return e;
}

/********** This should be used more. *************/
double 
arc_bezier_error_improved (const bezier_t &b)
{
	Pair<bezier_t> pair = b.halve ();
	point_t m = pair.second.p0;
	circle_t c (b.p0, m, b.p3);
	return MAX (arc_bezier_error (pair.first, c), arc_bezier_error (pair.second, c));
}


/********************************************
 *  TODO: use arc_bezier_error_improved     *
 *        for much cleaner code!!           *
 ********************************************/
static double 
binary_find_cut_L_old (const bezier_t &b,
                   double i,
                   double epsilon)
{
  double low, mid, high, cut_point, error;
  
  /* Find circle going through Bezier at i, (i + 1)/2, and 1. */
	point_t b_low = b.point(i);
	point_t b_mid = b.point((i + 1.0) / 2.0);
	point_t b_high = b.p3;
	circle_t c (b_low, b_mid, b_high);
//	printf("Error looks like %g\n", arc_bezier_error_improved (b.segment(i, 1))); 

	error = arc_bezier_error_improved (b.segment (i, 1));
 

  /* Compute error between Bezier [i, 1] and circle. */
	/* divide the curve into two */
	Pair<bezier_t> pair = b.split (i);
  pair = pair.second.halve();

  error = MAX (arc_bezier_error (pair.first, c), arc_bezier_error (pair.second, c));
//	printf("Arc error between %g and %g: %g.\n", i, 1.00, error);


  /* If error < epsilon, return 1. */
  if (error < epsilon)
		return 1.0;

  /* Use binary search to find a good cut_point such that error(bezier cut at cut_point) ~= epsilon. */
  low = i;
  high = 1.0;
  
  /* Perform [MAX_ITERS] steps of a binary search. */
  int count;
  for (count = 1; count <= MAX_ITERS; count++) {
		cut_point = (low + high) / 2.0;
    mid = (i + cut_point) / 2.0;


    /* Find circle going through Bezier at low, mid, and high. */
		b_low = b.point(i);
		b_mid = b.point(mid);
		b_high = b.point(cut_point);
		circle_t c (b_low, b_mid, b_high);


    /* Compute error between Bezier [i, cut_point] and circle. */
		bezier_t b_i_cut = b.segment(i, cut_point);
		pair = b_i_cut.halve();
  	error = MAX (arc_bezier_error (pair.first, c), arc_bezier_error (pair.second, c));
//		printf("  Arc error between %g and %g: %g.\n", i, cut_point, error);
		
    if (error == epsilon)
      return cut_point;
    if (error < epsilon)
			low = cut_point;
    else
      high = cut_point;
  }

	/* By now, mid should be close enough to the desired value. 
   * NOTE: We ***might*** be slightly above epsilon... */
	return cut_point;    

}


/********** NOT CURRENTLY USED. *************/
static double 
binary_find_cut_R_old (const bezier_t &b,
                   double j,
                   double epsilon)
{
  double low, mid, high, cut_point, error;
  
  /* Find circle going through Bezier at 0, j/2, and j. */
	point_t b_low = b.p0;
	point_t b_mid = b.point (j / 2.0);
	point_t b_high = b.point (j);
	circle_t c (b_low, b_mid, b_high);
 

  /* Compute error between Bezier [0, j] and circle. */
	/* divide the curve into two */
	Pair<bezier_t> pair = b.split (j);
  pair = pair.first.halve();

  error = MAX (arc_bezier_error (pair.first, c), arc_bezier_error (pair.second, c));
//	printf("Arc error between %g and %g: %g.\n", i, 1.00, error);


  /* If error < epsilon, return 0. */
  if (error < epsilon)
		return 0.0;

  /* Use binary search to find a good cut_point such that error(bezier cut at cut_point) ~= epsilon. */
  low = 0.0;
  high = j;
  
  /* Perform [MAX_ITERS] steps of a binary search. */
  int count;
  for (count = 1; count <= MAX_ITERS; count++) {
		cut_point = (low + high) / 2.0;
    mid = (cut_point + j) / 2.0;


    /* Find circle going through Bezier at low, mid, and high. */
		b_low = b.point(cut_point);
		b_mid = b.point(mid);
		b_high = b.point(j);
		circle_t c (b_low, b_mid, b_high);


    /* Compute error between Bezier [i, cut_point] and circle. */
		bezier_t b_cut_j = b.segment(cut_point, j);
		pair = b_cut_j.halve();
  	error = MAX (arc_bezier_error (pair.first, c), arc_bezier_error (pair.second, c));
//		printf("  Arc error between %g and %g: %g.\n", i, cut_point, error);
		
    if (error == epsilon)
      return cut_point;
    if (error < epsilon)
			high = cut_point;
    else
      low = cut_point;
  }

	/* By now, mid should be close enough to the desired value. 
   * NOTE: We ***might*** be slightly above epsilon... */
	return cut_point;    

}
 


/*******************************************************************************************************************/




static double 
binary_find_cut_L (const bezier_t &b,
                   double i,
                   double epsilon)
{
  double low, high, cut_point, error;  
  error = arc_bezier_error_improved (b.segment (i, 1));

  /* If error < epsilon, return 1. */
  if (error < epsilon)
		return 1.0;

  /* Use binary search to find a good cut_point such that error(bezier cut at cut_point) ~= epsilon. */
  low = i;
  high = 1.0;
  
  /* Perform [MAX_ITERS] steps of a binary search. */
  int count;
  for (count = 1; count <= MAX_ITERS; count++) {

 	  cut_point = (low + high) / 2.0;
  	error = arc_bezier_error_improved (b.segment (i, cut_point));

    if (error == epsilon)
      return cut_point;
    if (error < epsilon)
			low = cut_point;
    else
      high = cut_point;
  }

	/* By now, mid should be close enough to the desired value. 
   * NOTE: We ***might*** be slightly above epsilon... */
	return cut_point;    
}


static double 
binary_find_cut_R (const bezier_t &b,
                   double j,
                   double epsilon)
{
  double low, high, cut_point, error;  
  error = arc_bezier_error_improved (b.segment (0.0, j));

  /* If error < epsilon, return 1. */
  if (error < epsilon)
		return 0.0;

  /* Use binary search to find a good cut_point such that error(bezier cut at cut_point) ~= epsilon. */
  low = 0.0;
  high = j;
  
  /* Perform [MAX_ITERS] steps of a binary search. */
  int count;
  for (count = 1; count <= MAX_ITERS; count++) {

 	  cut_point = (low + high) / 2.0;
  	error = arc_bezier_error_improved (b.segment (cut_point, j));

    if (error == epsilon)
      return cut_point;
    if (error < epsilon)
			high = cut_point;
    else
      low = cut_point;
  }

	/* By now, mid should be close enough to the desired value. 
   * NOTE: We ***might*** be slightly above epsilon... */
	return cut_point;    
}


















static void 
find_cut_points_L (const bezier_t &b, double epsilon, deque<double> &left_cuts)
{  
	double t = 0;
	while (t < 1) {
		t = binary_find_cut_L (b, t, epsilon);
		left_cuts.push_back (t);
	}
}



static void
find_cut_points_R (const bezier_t &b, double epsilon, deque<double> &right_cuts)
{
	double t = 1;
	while (t > 0) {
		t = binary_find_cut_R (b, t, epsilon);
		right_cuts.push_front (t);
	}
}
static void
demo_curve (cairo_t *cr)
{
  int i;
  double line_width;
  cairo_path_t *path;
  cairo_path_data_t *data, current_point;

  cairo_set_source_rgb (cr, 1, 0, 0);
  cairo_fancy_stroke_preserve (cr);
  path = cairo_copy_path (cr);
  cairo_new_path (cr);

  cairo_path_print_stats (path);

  cairo_save (cr);
  line_width = cairo_get_line_width (cr);
  cairo_set_line_width (cr, line_width / 16);
  for (i=0; i < path->num_data; i += path->data[i].header.length) {
    data = &path->data[i];
    switch (data->header.type) {
    case CAIRO_PATH_MOVE_TO:
    case CAIRO_PATH_LINE_TO:
	current_point = data[1];
	break;
    case CAIRO_PATH_CURVE_TO:
	{
#define P(d) (point_t (d.point.x, d.point.y))
	  bezier_t b (P (current_point), P (data[1]), P (data[2]), P (data[3]));
#undef P

	  if (1)
	  {
	    /* divide the curve into two */
	    Pair<bezier_t> pair = b.halve ();
	    point_t m = pair.second.p0;

	    arc_t a0 (b.p0, m, b.p3, true);
	    arc_t a1 (m, b.p3, b.p0, true);
	    point_t cc (0,0);
	    double e0 = bezier_arc_error (pair.first, a0);
	    double e1 = bezier_arc_error (pair.second, a1);
	    double e = MAX (e0, e1);

	    //double e = bezier_arc_error (b, a);

	    printf ("%g %g = %g\n", e0, e1, e);

	    arc_t a (b.p0, b.p3, m, true);
	    circle_t c = a.circle ();

	    {
	      double t;
	      double e = 0;
	      for (t = 0; t <= 1; t += .001) {
		point_t p = b.point (t);
		e = MAX (e, fabs ((c.c - p).len () - c.r));
	      }
	      printf ("Actual arc max error %g\n", e);
	    }

	    cairo_save (cr);
	    cairo_set_source_rgba (cr, 0.0, 1.0, 0.0, 1.0);
	    cairo_demo_point (cr, m);
//	    cairo_demo_arc (cr, a);
	    cairo_restore (cr);
	  }

	  if (1) {
			/* Test binary cut. */
			deque<double> left_cuts, right_cuts;	
			find_cut_points_L (b, EPSILON, left_cuts);
			find_cut_points_R (b, EPSILON, right_cuts);
			left_cuts.pop_back ();
			right_cuts.pop_front ();
			

			int num_cut_ranges = left_cuts.size ();
			double cut_low [num_cut_ranges];
			double cut_high [num_cut_ranges];
			double cut_point [num_cut_ranges + 2];
			double arc_error [num_cut_ranges + 1];

			int cut_count = 0;
			cut_point [0] = 0;
			while (!left_cuts.empty()) {
				cut_low [cut_count] = right_cuts.front();
				cut_high [cut_count] = left_cuts.front();
				cut_point [cut_count + 1] = (cut_low [cut_count] + cut_high [cut_count]) / 2.0;
				arc_error [cut_count] = arc_bezier_error_improved (b.segment (cut_point [cut_count], cut_point [cut_count + 1]));
				printf("Cut range: [%g (%g) %g] ~ %g\n", cut_low [cut_count], cut_point [cut_count + 1], cut_high [cut_count], arc_error [cut_count]); 
				right_cuts.pop_front();
				left_cuts.pop_front();
				cut_count++;
			}
			cut_point [cut_count + 1] = 1;
			arc_error [cut_count] = arc_bezier_error_improved (b.segment (cut_point [cut_count], cut_point [cut_count + 1]));

			/* ..."Jiggle"...    -_-;;      */
			for (int jiggle_count = 1; jiggle_count < 10; jiggle_count++) {
				for (int cut_number = 0; cut_number < num_cut_ranges; cut_number++) {

					/* Step size is something like, |error[cn + 1] - error[cn]| / (2^(1+curvature[cut[cn+1]]) * epsilon. */
					vector_t prime = b.tangent (cut_point [cut_number]);
					vector_t prime2 = b.d_tangent (cut_point [cut_number]);
					double len = prime.len();
		      double curvature = (prime2.dy * prime.dx - prime2.dx * prime.dy) / (len*len*len);
					double step_size = fabs (arc_error [cut_number+1] - arc_error [cut_number]) / (pow (2, (1+curvature)) * EPSILON);
					if (arc_error [cut_number+1] > arc_error [cut_number])
						cut_point [cut_number+1] -= step_size * (cut_point [cut_number+1] - cut_low[cut_number]);
					else
						cut_point [cut_number+1] += step_size * (cut_high[cut_number] - cut_point [cut_number+1]);

					/* Update arc errors. */
					arc_error [cut_number] = arc_bezier_error_improved (b.segment (cut_point [cut_number], cut_point [cut_number + 1]));
				}
			}

			for (int cut_number = 0; cut_number < num_cut_ranges; cut_number++) {
				printf("Cut range: [%g (%g) %g] ~ %g\n", cut_low [cut_number], cut_point [cut_number + 1], cut_high [cut_number], arc_error [cut_number]); 
			}



			/* Draw the arcs. */
			double previous_cut = 0.0;
			double current_cut;
			for (cut_count = 0; cut_count <  num_cut_ranges + 1; cut_count++) {				
				current_cut = cut_point [cut_count + 1];
				printf(">> Beginning a new arc segment: %g to %g.\n", previous_cut, current_cut);

				bezier_t small_b = b.segment(previous_cut, current_cut);

				previous_cut = current_cut;

			  /* divide the curve into two */
			  Pair<bezier_t> pair = small_b.halve ();
			  point_t m = pair.second.p0;

			  arc_t a (small_b.p0, small_b.p3, m, true);
			  circle_t c = a.circle ();

			  double e0 = arc_bezier_error (pair.first, c);
			  double e1 = arc_bezier_error (pair.second, c);
			  double e = MAX (e0, e1);

			  printf   ("Estim. arc max error %g\n", e);

			  {
			    double t;
			    double e = 0;
			    for (t = 0; t <= 1; t += .001) {
			      point_t p = small_b.point (t);
			      e = MAX (e, fabs ((c.c - p).len () - c.r));
			    }
			    printf ("Actual arc max error %g\n", e);
			  }

			  cairo_save (cr);
			  cairo_set_source_rgba (cr, 0.0, 1.0, 0.0, 1.0);

			  cairo_set_line_width (cr, line_width * 0.5);
			  cairo_demo_arc (cr, a);

			  cairo_restore (cr);
			}



			/* Make arrays storing cut bounds, cut positions, and error values. */
/*			double cut_low [right_cuts.size()];
			double cut_high [left_cuts.size()];
			double cut_place [left_cuts.size() + 2];
			double arc_error [left_cuts.size() + 1];

			int cut_index = 0;

			cut_place [0] = 0;
			cut_place [left_cuts.size() + 1] = 1.0;
			printf ("The cut ranges are as follows:\n");
			while (!left_cuts.empty())
 		  {
				cut_low [cut_index] = right_cuts.front();
				cut_high [cut_index] = left_cuts.front();
				cut_place [cut_index + 1] = cut_high [cut_index]; // (cut_low [cut_index] + cut_high [cut_index]) / 2.0;
				arc_error [cut_index] = arc_bezier_error_improved (b.segment (cut_place[cut_index], cut_place [cut_index + 1]));

   		  printf("[%g (%g) %g] ~ %g\n", cut_low [cut_index], cut_place[cut_index + 1], cut_high[cut_index], arc_error[cut_index]);
				
   		  left_cuts.pop_front();
			 	right_cuts.pop_front();

				cut_index++;
  		}  
			printf("\n"); */

	  }

	  if (0) {
	    for (double t = 0; t <= 1; t += .01)
	    {
	      point_t p = b.point (t);
	      circle_t cv = b.osculating_circle (t);
	      cairo_move_to (cr, p.x, p.y);
	      cairo_line_to (cr, cv.c.x, cv.c.y);
	    }
	  }
	}
	current_point = data[3];
	break;
    case CAIRO_PATH_CLOSE_PATH:
	break;
    default:
	break;
    }
  }
  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_stroke (cr);
  cairo_restore (cr);

#if 0
  cairo_save (cr);
  for (i=0; i < path->num_data; i += path->data[i].header.length) {
    data = &path->data[i];
    switch (data->header.type) {
    case CAIRO_PATH_MOVE_TO:
	cairo_move_to (cr, data[1].point.x, data[1].point.y);
	break;
    case CAIRO_PATH_LINE_TO:
	cairo_line_to (cr, data[1].point.x, data[1].point.y);
	break;
    case CAIRO_PATH_CURVE_TO:
	cairo_curve_to (cr, data[1].point.x, data[1].point.y,
			    data[2].point.x, data[2].point.y,
			    data[3].point.x, data[3].point.y);
	break;
    case CAIRO_PATH_CLOSE_PATH:
	cairo_close_path (cr);
	break;
    default:
	break;
    }
  }
  cairo_stroke (cr);
  cairo_restore (cr);
#endif
  cairo_path_destroy (path);
}



static void
demo_curve_good (cairo_t *cr)
{
  int i;
  double line_width;
  cairo_path_t *path;
  cairo_path_data_t *data, current_point;

  cairo_set_source_rgb (cr, 1, 0, 0);
  cairo_fancy_stroke_preserve (cr);
  path = cairo_copy_path (cr);
  cairo_new_path (cr);

  cairo_path_print_stats (path);

  cairo_save (cr);
  line_width = cairo_get_line_width (cr);
  cairo_set_line_width (cr, line_width / 16);
  for (i=0; i < path->num_data; i += path->data[i].header.length) {
    data = &path->data[i];
    switch (data->header.type) {
    case CAIRO_PATH_MOVE_TO:
    case CAIRO_PATH_LINE_TO:
	current_point = data[1];
	break;
    case CAIRO_PATH_CURVE_TO:
	{
#define P(d) (point_t (d.point.x, d.point.y))
	  bezier_t b (P (current_point), P (data[1]), P (data[2]), P (data[3]));
#undef P

	  if (1)
	  {
	      /* Test binary cut. */
	      deque<double> left_cuts, right_cuts;		
	      find_cut_points_L (b, EPSILON, left_cuts);

	      double previous_cut = 0.0;
	      double current_cut;
	      while (!left_cuts.empty()) {				
		      current_cut = left_cuts.front();
		      printf(">> Beginning a new arc segment: %g to %g.\n", previous_cut, current_cut);

		      left_cuts.pop_front();
		      circle_t cm (b.point(previous_cut), b.point((previous_cut + current_cut) / 2.0), b.point(current_cut));
		      bezier_t small_b = b.segment(previous_cut, current_cut);
      
		      double t;
		      for (t = 0; t <= 1; t += .01) {
			point_t p = small_b.point (t);

			/* Draw a line from the curve to the centre of the circle. */
			cairo_set_source_rgb (cr, 0, 0, 1);
			cairo_move_to (cr, p.x, p.y);
			cairo_line_to (cr, cm.c.x, cm.c.y);

			cairo_stroke (cr);
		      }
		      previous_cut = current_cut;



		      /* divide the curve into two */
		Pair<bezier_t> pair = small_b.halve ();
		point_t m = pair.second.p0;

		circle_t c (small_b.p0, m, small_b.p3);

		double e0 = arc_bezier_error (pair.first, c);
		double e1 = arc_bezier_error (pair.second, c);
		double e = MAX (e0, e1);

		printf   ("Estim. arc max error %g\n", e);

		{
		  double t;
		  double e = 0;
		  for (t = 0; t <= 1; t += .001) {
				      point_t p = small_b.point (t);
				      e = MAX (e, fabs ((c.c - p).len () - c.r));
		  }
		  printf ("Actual arc max error %g\n", e);
		}

		      cairo_save (cr);
		cairo_set_source_rgba (cr, 0.0, 1.0, 0.0, 1.0);

		cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
		cairo_move_to (cr, small_b.p0.x, small_b.p0.y);
		cairo_rel_line_to (cr, 0, 0);
		cairo_set_line_width (cr, line_width * 2);
		cairo_stroke (cr);

		cairo_set_line_width (cr, line_width * 0.5);

		{
		  arc_t a (small_b.p0, small_b.p3, m, true);
		  circle_t c  = a.circle ();

		  double a0 = (a.p0 - c.c).angle ();
		  double a1 = (a.p1 - c.c).angle ();
		  printf("Arc from %g to %g.\n", a0, a1);

		  if (a0 < a1)
		    cairo_arc (cr, c.c.x, c.c.y, c.r, a0, a1);
		  else
		    cairo_arc_negative (cr, c.c.x, c.c.y, c.r, a0, a1);

		}

		cairo_stroke (cr);

		cairo_restore (cr);

	      }



	      /* Make arrays storing cut bounds, cut positions, and error values. */
/*			double cut_low [right_cuts.size()];
	      double cut_high [left_cuts.size()];
	      double cut_place [left_cuts.size() + 2];
	      double arc_error [left_cuts.size() + 1];

	      int cut_index = 0;

	      cut_place [0] = 0;
	      cut_place [left_cuts.size() + 1] = 1.0;
	      printf ("The cut ranges are as follows:\n");
	      while (!left_cuts.empty())
	{
		      cut_low [cut_index] = right_cuts.front();
		      cut_high [cut_index] = left_cuts.front();
		      cut_place [cut_index + 1] = cut_high [cut_index]; // (cut_low [cut_index] + cut_high [cut_index]) / 2.0;
		      arc_error [cut_index] = arc_bezier_error_improved (b.segment (cut_place[cut_index], cut_place [cut_index + 1]));

	printf("[%g (%g) %g] ~ %g\n", cut_low [cut_index], cut_place[cut_index + 1], cut_high[cut_index], arc_error[cut_index]);
		      
	left_cuts.pop_front();
		      right_cuts.pop_front();

		      cut_index++;
      }  
	      printf("\n"); */

	      
	  }

	  {
	    for (double t = 0; t <= 1; t += .05)
	    {
	      point_t p = b.point (t);
	      circle_t cv = b.osculating_circle (t);
	      cairo_move_to (cr, p.x, p.y);
	      cairo_line_to (cr, cv.c.x, cv.c.y);
	    }
	  }
	}
	current_point = data[3];
	break;
    case CAIRO_PATH_CLOSE_PATH:
	break;
    default:
	break;
    }
  }
  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_stroke (cr);
  cairo_restore (cr);

#if 0
  cairo_save (cr);
  for (i=0; i < path->num_data; i += path->data[i].header.length) {
    data = &path->data[i];
    switch (data->header.type) {
    case CAIRO_PATH_MOVE_TO:
	cairo_move_to (cr, data[1].point.x, data[1].point.y);
	break;
    case CAIRO_PATH_LINE_TO:
	cairo_line_to (cr, data[1].point.x, data[1].point.y);
	break;
    case CAIRO_PATH_CURVE_TO:
	cairo_curve_to (cr, data[1].point.x, data[1].point.y,
			    data[2].point.x, data[2].point.y,
			    data[3].point.x, data[3].point.y);
	break;
    case CAIRO_PATH_CLOSE_PATH:
	cairo_close_path (cr);
	break;
    default:
	break;
    }
  }
  cairo_stroke (cr);
  cairo_restore (cr);
#endif
  cairo_path_destroy (path);
}

static void
draw_dream (cairo_t *cr)
{
  printf ("SAMPLE: dream line\n");

  cairo_save (cr);
  cairo_new_path (cr);

  cairo_move_to (cr, 50, 650);

  cairo_rel_line_to (cr, 250, 50);
  cairo_rel_curve_to (cr, 250, 50, 600, -50, 600, -250);
  cairo_rel_curve_to (cr, 0, -400, -300, -100, -800, -300);

  cairo_set_line_width (cr, 5);
  cairo_set_source_rgba (cr, 0.3, 1.0, 0.3, 0.3);

  demo_curve (cr);

  cairo_restore (cr);
}

static void
draw_raskus_simple (cairo_t *cr)
{
  printf ("SAMPLE: raskus simple\n");

  cairo_save (cr);
  cairo_new_path (cr);

  cairo_save (cr);
  cairo_translate (cr, -1300, 500);
  cairo_scale (cr, 200, -200);
  cairo_translate (cr, -10, -1);
  cairo_move_to (cr, 16.9753, .7421);
  cairo_curve_to (cr, 18.2203, 2.2238, 21.0939, 2.4017, 23.1643, 1.6148);
  cairo_restore (cr);

  cairo_set_line_width (cr, 2.0);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);

  demo_curve (cr);

  cairo_restore (cr);
}

static void
draw_raskus_complicated (cairo_t *cr)
{
  printf ("SAMPLE: raskus complicated\n");

  cairo_save (cr);
  cairo_new_path (cr);

  cairo_save (cr);
  cairo_translate (cr, -500, 400);
  cairo_scale (cr, 100, -100);
  cairo_translate (cr, -10, -1);
  cairo_curve (cr, bezier_t (point_t (17.5415, 0.9003),
			     point_t (18.4778, 3.8448),
			     point_t (22.4037, -0.9109),
			     point_t (22.563, 0.7782)));
  cairo_restore (cr);

  cairo_set_line_width (cr, 5.0); //2.0
  cairo_set_source_rgba (cr, 0.3, 1.0, 0.3, 1.0);

  demo_curve (cr);

  cairo_restore (cr);
}

static void
draw_raskus_complicated2 (cairo_t *cr)
{
  printf ("SAMPLE: raskus complicated2\n");

  cairo_save (cr);
  cairo_new_path (cr);

  cairo_save (cr);
  cairo_translate (cr, -500, 400);
  cairo_scale (cr, 100, -100);
  cairo_translate (cr, -10, -1);
  cairo_move_to (cr, 18.4778, 3.8448);
  cairo_curve_to (cr, 17.5415, 0.9003, 22.563, 0.7782, 22.4037, -0.9109);
  cairo_restore (cr);

  cairo_set_line_width (cr, 5.0); //2.0
  cairo_set_source_rgba (cr, 0.3, 1.0, 0.3, 1.0);

  demo_curve (cr);

  cairo_restore (cr);
}


static void
draw_skewed (cairo_t *cr)
{
  printf ("SAMPLE: skewed\n");

  cairo_save (cr);
  cairo_new_path (cr);

  cairo_move_to (cr, 50, 380);
  cairo_scale (cr, 2, 2);
  cairo_rel_curve_to (cr, 0, -100, 250, -50, 330, 10);

  cairo_set_line_width (cr, 2.0);
  cairo_set_source_rgba (cr, 0.3, 1.0, 0.3, 1.0);

  demo_curve (cr);

  cairo_restore (cr);
}

int main (int argc, char **argv)
{
  cairo_t *cr;
  char *filename;
  cairo_status_t status;
  cairo_surface_t *surface;

  if (argc != 2) {
    fprintf (stderr, "Usage: arc OUTPUT_FILENAME\n");
    return 1;
  }

  filename = argv[1];

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
					1400, 1000);
  cr = cairo_create (surface);

  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_paint (cr);

//  draw_skewed (cr);
  draw_raskus_simple (cr);
//  draw_raskus_complicated (cr);
//  draw_raskus_complicated2 (cr);
//  draw_dream (cr);

  cairo_destroy (cr);

  status = cairo_surface_write_to_png (surface, filename);
  cairo_surface_destroy (surface);

  if (status != CAIRO_STATUS_SUCCESS)
    {
      g_printerr ("Could not save png to '%s'\n", filename);
      return 1;
    }

  return 0;
}
