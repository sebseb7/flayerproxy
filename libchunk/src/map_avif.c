#include "internal.h"

#include <avif/avif.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void lc_avif_log(const char *path, const char *step, avifResult ar) {
  if (ar != AVIF_RESULT_OK) {
    fprintf(stderr, "avif encode %s (%s): %s\n", step, path ? path : "?", avifResultToString(ar));
  }
}

static lc_status lc_avif_fail(avifRGBImage *rgb, avifImage *image, avifEncoder *encoder,
                              avifDecoder *decoder) {
  if (rgb) avifRGBImageFreePixels(rgb);
  if (image) avifImageDestroy(image);
  if (encoder) avifEncoderDestroy(encoder);
  if (decoder) avifDecoderDestroy(decoder);
  return LC_ERR_INVALID;
}

lc_status lc_write_rgb_avif(const char *path, const uint8_t *rgb, int w, int h) {
  if (!path || !rgb || w <= 0 || h <= 0) return LC_ERR_INVALID;

  avifImage *image = avifImageCreate((uint32_t)w, (uint32_t)h, 8, AVIF_PIXEL_FORMAT_YUV444);
  if (!image) return LC_ERR_OOM;

  avifRGBImage rgbimg;
  memset(&rgbimg, 0, sizeof rgbimg);
  avifRGBImageSetDefaults(&rgbimg, image);
  rgbimg.format = AVIF_RGB_FORMAT_RGB;
  rgbimg.depth = 8;
  rgbimg.chromaDownsampling = AVIF_CHROMA_DOWNSAMPLING_AVERAGE;

  avifResult   ar = avifRGBImageAllocatePixels(&rgbimg);
  if (ar != AVIF_RESULT_OK) {
    lc_avif_log(path, "allocate rgb", ar);
    avifImageDestroy(image);
    return LC_ERR_OOM;
  }

  for (int y = 0; y < h; y++) {
    memcpy(rgbimg.pixels + (size_t)y * (size_t)rgbimg.rowBytes, rgb + (size_t)y * (size_t)w * 3,
           (size_t)w * 3);
  }

  ar = avifImageRGBToYUV(image, &rgbimg);
  avifRGBImageFreePixels(&rgbimg);
  if (ar != AVIF_RESULT_OK) {
    lc_avif_log(path, "rgb to yuv", ar);
    avifImageDestroy(image);
    return LC_ERR_INVALID;
  }

  avifEncoder *encoder = avifEncoderCreate();
  if (!encoder) {
    avifImageDestroy(image);
    return LC_ERR_OOM;
  }

  encoder->minQuantizer = AVIF_QUANTIZER_LOSSLESS;
  encoder->maxQuantizer = AVIF_QUANTIZER_LOSSLESS;
  encoder->speed = AVIF_SPEED_DEFAULT;

  ar = avifEncoderAddImage(encoder, image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
  avifImageDestroy(image);
  if (ar != AVIF_RESULT_OK) {
    lc_avif_log(path, "add image", ar);
    avifEncoderDestroy(encoder);
    return LC_ERR_INVALID;
  }

  avifRWData output = AVIF_DATA_EMPTY;
  ar = avifEncoderFinish(encoder, &output);
  avifEncoderDestroy(encoder);
  if (ar != AVIF_RESULT_OK) {
    lc_avif_log(path, "finish", ar);
    avifRWDataFree(&output);
    return LC_ERR_INVALID;
  }

  FILE *f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "avif encode fopen (%s): %s\n", path, strerror(errno));
    avifRWDataFree(&output);
    return LC_ERR_INVALID;
  }

  size_t out_len = output.size;
  size_t written = fwrite(output.data, 1, out_len, f);
  int io_err = ferror(f);
  fclose(f);
  avifRWDataFree(&output);

  if (io_err || written != out_len) {
    fprintf(stderr, "avif encode fwrite (%s): wrote %zu of %zu\n", path, written, out_len);
    return LC_ERR_INVALID;
  }
  return LC_OK;
}

lc_status lc_read_rgb_avif(const char *path, uint8_t **rgb, int *w, int *h) {
  if (!path || !rgb || !w || !h) return LC_ERR_INVALID;

  avifDecoder *decoder = avifDecoderCreate();
  avifImage *image = avifImageCreateEmpty();
  if (!decoder || !image) {
    if (image) avifImageDestroy(image);
    if (decoder) avifDecoderDestroy(decoder);
    return LC_ERR_OOM;
  }

  avifResult ar = avifDecoderReadFile(decoder, image, path);
  if (ar != AVIF_RESULT_OK) return lc_avif_fail(NULL, image, NULL, decoder);

  int img_w = (int)image->width;
  int img_h = (int)image->height;
  if (img_w <= 0 || img_h <= 0) return lc_avif_fail(NULL, image, NULL, decoder);

  avifRGBImage rgbimg;
  memset(&rgbimg, 0, sizeof rgbimg);
  avifRGBImageSetDefaults(&rgbimg, image);
  rgbimg.format = AVIF_RGB_FORMAT_RGB;
  rgbimg.depth = 8;

  ar = avifRGBImageAllocatePixels(&rgbimg);
  if (ar != AVIF_RESULT_OK) return lc_avif_fail(&rgbimg, image, NULL, decoder);

  ar = avifImageYUVToRGB(image, &rgbimg);
  if (ar != AVIF_RESULT_OK) return lc_avif_fail(&rgbimg, image, NULL, decoder);

  uint8_t *out = (uint8_t *)malloc((size_t)img_w * (size_t)img_h * 3);
  if (!out) return lc_avif_fail(&rgbimg, image, NULL, decoder);

  for (int y = 0; y < img_h; y++) {
    memcpy(out + (size_t)y * (size_t)img_w * 3, rgbimg.pixels + (size_t)y * (size_t)rgbimg.rowBytes,
           (size_t)img_w * 3);
  }

  avifRGBImageFreePixels(&rgbimg);
  avifImageDestroy(image);
  avifDecoderDestroy(decoder);

  *rgb = out;
  *w = img_w;
  *h = img_h;
  return LC_OK;
}
