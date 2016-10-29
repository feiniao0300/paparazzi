/*
 * Copyright (C) Freek van Tienen
 *
 * This file is part of paparazzi
 *
 * paparazzi is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * paparazzi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with paparazzi; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
/**
 * @file "modules/computer_vision/cv_ae_awb.c"
 * @author Freek van Tienen
 * Auto exposure and Auto white balancing for the Bebop 1 and 2
 */

#include "modules/computer_vision/cv_ae_awb.h"
#include "boards/bebop.h"
#include "boards/bebop/mt9f002.h"
#include "lib/isp/libisp.h"

#define MAX_HIST_Y (256-10)

#define sgn(x) (float)((x < 0) ? -1 : (x > 0))

#ifndef CV_AUTO_EXPOSURE
#define CV_AUTO_EXPOSURE true
#endif

#ifndef CV_AUTO_WHITE_BALANCE
#define CV_AUTO_WHITE_BALANCE true
#endif

#define CV_AWB_MIN_GAIN 2
#define CV_AWB_MAX_GAIN 75

void cv_ae_awb_init(void) {}

void cv_ae_awb_periodic(void)
{
  struct isp_yuv_stats_t yuv_stats;

  if (isp_get_statistics_yuv(&yuv_stats) == 0) {
#if CV_AUTO_EXPOSURE
    // Calculate the CDF based on the histogram
    uint32_t cdf[MAX_HIST_Y];
    cdf[0] = yuv_stats.ae_histogram_Y[0];
    for (int i = 1; i < MAX_HIST_Y; i++) {
      cdf[i] = cdf[i - 1] + yuv_stats.ae_histogram_Y[i];
    }

    // Calculate bright and saturated pixels
    uint32_t bright_pixels = cdf[MAX_HIST_Y - 1] - cdf[MAX_HIST_Y - 21]; // Top 20 bins
    uint32_t saturated_pixels = cdf[MAX_HIST_Y - 1] - cdf[MAX_HIST_Y - 6]; // top 5 bins
    uint32_t target_bright_pixels = yuv_stats.nb_valid_Y / 10; // 10%
    uint32_t max_saturated_pixels = yuv_stats.nb_valid_Y / 400; // 0.25%
    float adjustment = 1.0f;

    // Fix saturated pixels
    if (saturated_pixels > max_saturated_pixels) {
      adjustment = 1.0f - ((float)(saturated_pixels - max_saturated_pixels)) / yuv_stats.nb_valid_Y;
    } else if (bright_pixels + target_bright_pixels / 10 < target_bright_pixels) {  // Fix bright pixels if outside of 10% of target
      // increase brightness to try and hit the desired number of well exposed pixels
      int l = MAX_HIST_Y - 11;
      while (bright_pixels < target_bright_pixels && l > 0) {
        bright_pixels += cdf[l];
        bright_pixels -= cdf[l - 1];
        l--;
      }

      adjustment = (float)(MAX_HIST_Y - 11 + 1) / (l + 1);
    } else if (bright_pixels - target_bright_pixels / 10 > target_bright_pixels) {  // Fix bright pixels if outside of 10% of target
      // decrease brightness to try and hit the desired number of well exposed pixels
      int l = MAX_HIST_Y - 20;
      while (bright_pixels > target_bright_pixels && l < MAX_HIST_Y) {
        bright_pixels -= cdf[l];
        bright_pixels += cdf[l - 1];
        l++;
      }

      adjustment = (float)(MAX_HIST_Y - 20) / l;
      adjustment *= adjustment;   // speedup
    }

    // Calculate exposure
    Bound(adjustment, 1 / 16.0f, 4.0);
    mt9f002.target_exposure = mt9f002.real_exposure * adjustment;
    mt9f002_set_exposure(&mt9f002);
#endif

#if CV_AUTO_WHITE_BALANCE
    // Calculate AWB and project from original scale [0,255] onto more typical scale[-0.5,0.5]
    float avgU = ((float) yuv_stats.awb_sum_U / (float) yuv_stats.awb_nb_grey_pixels) / 256. - 0.5;
    float avgV = ((float) yuv_stats.awb_sum_V / (float) yuv_stats.awb_nb_grey_pixels) / 256. - 0.5;
    float threshold = 0.002f;
    float gain = 1.;
    bool changed = false;

    if (fabs(avgU) > threshold){
      mt9f002.gain_blue -= gain * avgU;
      changed = true;
    }
    if (fabs(avgV) > threshold){
      mt9f002.gain_red -= gain * avgV;
      changed = true;
    }

    if (changed){
      Bound(mt9f002.gain_blue, 2, 75);
      Bound(mt9f002.gain_red, 2, 75);
      mt9f002_set_gains(&mt9f002);
    }
#endif
  }
}
