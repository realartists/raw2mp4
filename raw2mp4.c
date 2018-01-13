#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <x264.h>
#include <lsmash.h>

// API
// all API fns return zero on success, non-zero on failure.
extern int CompressionSessionOpen(const char *output_path, int w, int h);
extern int CompressionSessionAddFrame(uint8_t *rgba); // len must be w * h * 4
extern int CompressionSessionFinish(void);

// Globals
typedef struct {
    x264_param_t param;
    x264_picture_t pic;
    x264_t *h;
    int i_frame;
    int i_frames_written;
    int64_t first_dts;
    int64_t last_dts;
    int64_t largest_pts;
    int64_t second_largest_pts;
} encoder_state_t;
static encoder_state_t encoder_state;

typedef struct {
    lsmash_root_t *p_root;
    lsmash_video_summary_t *summary;
    int b_stdout;
    uint32_t i_movie_timescale;
    uint32_t i_video_timescale;
    uint32_t i_track;
    uint32_t i_sample_entry;
    uint64_t i_time_inc;
    int64_t i_start_offset;
    uint64_t i_first_cts;
    uint64_t i_prev_dts;
    uint32_t i_sei_size;
    uint8_t *p_sei_buffer;
    int i_numframe;
    int64_t i_init_delta;
    int i_delay_frames;
    int b_dts_compress;
    int i_dts_compress_multiplier;
    int b_use_recovery;
    int b_fragments;
    lsmash_file_parameters_t file_param;
} mp4_state_t;
static mp4_state_t mp4_state;

#define CHK(cond, msg, ...) \
do { \
    int result = (cond); \
    if (!result) { \
        fprintf(stderr, "CHK failed: " msg "\n", ##__VA_ARGS__); \
        abort(); \
        return 1; \
    } \
} while (0);

static int mp4_write_headers(x264_nal_t *p_nal);


int CompressionSessionOpen(const char *output_path, int w, int h) {
    // Configure x264 encoder
    x264_param_default_preset(&encoder_state.param, "ultrafast", NULL);
    encoder_state.param.i_bitdepth = 8;
    encoder_state.param.i_csp = X264_CSP_I420;
    encoder_state.param.i_width  = w;
    encoder_state.param.i_height = h;
    encoder_state.param.b_vfr_input = 1;
    encoder_state.param.i_fps_num = 15;
    encoder_state.param.i_fps_den = 1;
    encoder_state.param.i_timebase_num = encoder_state.param.i_fps_den;
    encoder_state.param.i_timebase_den = encoder_state.param.i_fps_num;
    encoder_state.param.b_repeat_headers = 0;
    encoder_state.param.b_annexb = 0;
    
    CHK(x264_param_apply_profile(&encoder_state.param, "baseline") == 0, "apply profile");
        
    CHK(x264_picture_alloc(&encoder_state.pic,  encoder_state.param.i_csp, encoder_state.param.i_width, encoder_state.param.i_height) == 0, "picture alloc");
    
    encoder_state.h = x264_encoder_open(&encoder_state.param);
    CHK(encoder_state.h != NULL, "encoder open");
    
    // Configure lsmash
    mp4_state.b_dts_compress = 0;
    mp4_state.b_use_recovery = 0;
    mp4_state.b_fragments = 0;
    mp4_state.b_stdout = 0;
    
    mp4_state.p_root = lsmash_create_root();
    
    CHK(lsmash_open_file(output_path, 0, &mp4_state.file_param) == 0, "Unable to open file");
    
    mp4_state.summary = (lsmash_video_summary_t *)lsmash_create_summary(LSMASH_SUMMARY_TYPE_VIDEO);
    
    mp4_state.summary->sample_type = ISOM_CODEC_TYPE_AVC1_VIDEO;
    
    mp4_state.i_delay_frames = 0;
    mp4_state.i_dts_compress_multiplier = 1;
    
    uint64_t i_media_timescale;
    i_media_timescale = (uint64_t)encoder_state.param.i_timebase_den;
    mp4_state.i_time_inc = (uint64_t)encoder_state.param.i_timebase_num;
    
    lsmash_brand_type brands[6] = { 0 };
    uint32_t brand_count = 0;
    brands[brand_count++] = ISOM_BRAND_TYPE_MP42;
    brands[brand_count++] = ISOM_BRAND_TYPE_MP41;
    brands[brand_count++] = ISOM_BRAND_TYPE_ISOM;
    
    lsmash_file_parameters_t *file_param = &mp4_state.file_param;
    file_param->major_brand     = brands[0];
    file_param->brands              = brands;
    file_param->brand_count     = brand_count;
    file_param->minor_version = 0;
    CHK(lsmash_set_file(mp4_state.p_root, file_param) != NULL, "failed to add output file");
    
    lsmash_movie_parameters_t movie_param;
    lsmash_initialize_movie_parameters( &movie_param );
    CHK(lsmash_set_movie_parameters(mp4_state.p_root, &movie_param) == 0, "failed to set movie parameters");
    
    mp4_state.i_movie_timescale = lsmash_get_movie_timescale( mp4_state.p_root );
    CHK(mp4_state.i_movie_timescale, "movie timescale");

    mp4_state.i_track = lsmash_create_track(mp4_state.p_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK);
    CHK(mp4_state.i_track != 0, "failed to create a video track");
    
    mp4_state.summary->width = encoder_state.param.i_width;
    mp4_state.summary->height = encoder_state.param.i_height;
    uint32_t i_display_width = encoder_state.param.i_width << 16;
    uint32_t i_display_height = encoder_state.param.i_height << 16;
    
    if (encoder_state.param.vui.i_sar_width && encoder_state.param.vui.i_sar_height) {
        double sar = (double)encoder_state.param.vui.i_sar_width / encoder_state.param.vui.i_sar_height;
        if (sar > 1.0) {
            i_display_width *= sar;
        } else {
            i_display_height /= sar;
        }
        mp4_state.summary->par_h = encoder_state.param.vui.i_sar_width;
        mp4_state.summary->par_v = encoder_state.param.vui.i_sar_height;
    }
    mp4_state.summary->color.primaries_index = encoder_state.param.vui.i_colorprim;
    mp4_state.summary->color.transfer_index = encoder_state.param.vui.i_transfer;
    mp4_state.summary->color.matrix_index        = encoder_state.param.vui.i_colmatrix >= 0 ? encoder_state.param.vui.i_colmatrix : ISOM_MATRIX_INDEX_UNSPECIFIED;
    mp4_state.summary->color.full_range          = encoder_state.param.vui.b_fullrange >= 0 ? encoder_state.param.vui.b_fullrange : 0;

        /* Set video track parameters. */
    lsmash_track_parameters_t track_param;
    lsmash_initialize_track_parameters( &track_param );
    lsmash_track_mode track_mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
    track_param.mode = track_mode;
    track_param.display_width = i_display_width;
    track_param.display_height = i_display_height;
    CHK( lsmash_set_track_parameters( mp4_state.p_root, mp4_state.i_track, &track_param ) == 0,
                                     "failed to set track parameters for video.\n" );
    
    /* Set video media parameters. */
    lsmash_media_parameters_t media_param;
    lsmash_initialize_media_parameters( &media_param );
    media_param.timescale = (uint32_t)i_media_timescale;
    media_param.media_handler_name = "L-SMASH Video Media Handler";
    if( mp4_state.b_use_recovery )
    {
            media_param.roll_grouping = encoder_state.param.b_intra_refresh;
            media_param.rap_grouping = encoder_state.param.b_open_gop;
    }
    CHK( lsmash_set_media_parameters( mp4_state.p_root, mp4_state.i_track, &media_param ) == 0,
                                     "failed to set media parameters for video.\n" );
    mp4_state.i_video_timescale = lsmash_get_media_timescale( mp4_state.p_root, mp4_state.i_track );
    CHK( mp4_state.i_video_timescale != 0, "media timescale for video is broken.\n" );
    
    /* headers */
    x264_nal_t *headers;
    int i_nal;
    CHK(x264_encoder_headers(encoder_state.h, &headers, &i_nal) > 0, "x264_encoder_headers");
    mp4_write_headers(headers);    
    
    return 0;
}

static int mp4_write_headers(x264_nal_t *p_nal)
{
    #define H264_NALU_LENGTH_SIZE 4
    
    mp4_state_t *p_mp4 = &mp4_state;

    uint32_t sps_size = p_nal[0].i_payload - H264_NALU_LENGTH_SIZE;
    uint32_t pps_size = p_nal[1].i_payload - H264_NALU_LENGTH_SIZE;
    uint32_t sei_size = p_nal[2].i_payload;

    uint8_t *sps = p_nal[0].p_payload + H264_NALU_LENGTH_SIZE;
    uint8_t *pps = p_nal[1].p_payload + H264_NALU_LENGTH_SIZE;
    uint8_t *sei = p_nal[2].p_payload;

    lsmash_codec_specific_t *cs = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264,
                                                                     LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );

    lsmash_h264_specific_parameters_t *param = (lsmash_h264_specific_parameters_t *)cs->data.structured;
    param->lengthSizeMinusOne = H264_NALU_LENGTH_SIZE - 1;

    /* SPS
     * The remaining parameters are automatically set by SPS. */
    if( lsmash_append_h264_parameter_set( param, H264_PARAMETER_SET_TYPE_SPS, sps, sps_size ) )
    {
        return -1;
    }

    /* PPS */
    if( lsmash_append_h264_parameter_set( param, H264_PARAMETER_SET_TYPE_PPS, pps, pps_size ) )
    {
        return -1;
    }

    if( lsmash_add_codec_specific_data( (lsmash_summary_t *)p_mp4->summary, cs ) )
    {
        return -1;
    }

    lsmash_destroy_codec_specific_data( cs );

    /* Additional extensions */
    /* Bitrate info */
    cs = lsmash_create_codec_specific_data( LSMASH_CODEC_SPECIFIC_DATA_TYPE_ISOM_VIDEO_H264_BITRATE,
                                            LSMASH_CODEC_SPECIFIC_FORMAT_STRUCTURED );
    if( cs )
        lsmash_add_codec_specific_data( (lsmash_summary_t *)p_mp4->summary, cs );
    lsmash_destroy_codec_specific_data( cs );

    p_mp4->i_sample_entry = lsmash_add_sample_entry( p_mp4->p_root, p_mp4->i_track, p_mp4->summary );
    CHK( p_mp4->i_sample_entry != 0,
                     "failed to add sample entry for video.\n" );

    /* SEI */
    p_mp4->p_sei_buffer = malloc( sei_size );
    CHK( p_mp4->p_sei_buffer != NULL,
                     "failed to allocate sei transition buffer.\n" );
    memcpy( p_mp4->p_sei_buffer, sei, sei_size );
    p_mp4->i_sei_size = sei_size;

    return sei_size + sps_size + pps_size;
}

static int mp4_write_frame(uint8_t *p_nalu, int i_size, x264_picture_t *p_picture) {
    mp4_state_t *p_mp4 = &mp4_state;
    uint64_t dts, cts;

    if( !p_mp4->i_numframe )
    {
        p_mp4->i_start_offset = p_picture->i_dts * -1;
        p_mp4->i_first_cts = p_mp4->b_dts_compress ? 0 : p_mp4->i_start_offset * p_mp4->i_time_inc;
        if( p_mp4->b_fragments )
        {
            lsmash_edit_t edit;
            edit.duration   = ISOM_EDIT_DURATION_UNKNOWN32;     /* QuickTime doesn't support 64bit duration. */
            edit.start_time = p_mp4->i_first_cts;
            edit.rate       = ISOM_EDIT_MODE_NORMAL;
            CHK( lsmash_create_explicit_timeline_map( p_mp4->p_root, p_mp4->i_track, edit ) == 0,
                            "failed to set timeline map for video.\n" );
        }
    }

    lsmash_sample_t *p_sample = lsmash_create_sample( i_size + p_mp4->i_sei_size );
    CHK( p_sample != NULL,
                     "failed to create a video sample data.\n" );

    if( p_mp4->p_sei_buffer )
    {
        memcpy( p_sample->data, p_mp4->p_sei_buffer, p_mp4->i_sei_size );
        free( p_mp4->p_sei_buffer );
        p_mp4->p_sei_buffer = NULL;
    }

    memcpy( p_sample->data + p_mp4->i_sei_size, p_nalu, i_size );
    p_mp4->i_sei_size = 0;

    if( p_mp4->b_dts_compress )
    {
        if( p_mp4->i_numframe == 1 )
            p_mp4->i_init_delta = (p_picture->i_dts + p_mp4->i_start_offset) * p_mp4->i_time_inc;
        dts = p_mp4->i_numframe > p_mp4->i_delay_frames
            ? p_picture->i_dts * p_mp4->i_time_inc
            : p_mp4->i_numframe * (p_mp4->i_init_delta / p_mp4->i_dts_compress_multiplier);
        cts = p_picture->i_pts * p_mp4->i_time_inc;
    }
    else
    {
        dts = (p_picture->i_dts + p_mp4->i_start_offset) * p_mp4->i_time_inc;
        cts = (p_picture->i_pts + p_mp4->i_start_offset) * p_mp4->i_time_inc;
    }

    p_sample->dts = dts;
    p_sample->cts = cts;
    p_sample->index = p_mp4->i_sample_entry;
    p_sample->prop.ra_flags = p_picture->b_keyframe ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;

    if( p_mp4->b_fragments && p_mp4->i_numframe && p_sample->prop.ra_flags != ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE )
    {
        CHK( lsmash_flush_pooled_samples( p_mp4->p_root, p_mp4->i_track, (uint32_t)(p_sample->dts - p_mp4->i_prev_dts) ) == 0,
                         "failed to flush the rest of samples.\n" );
        CHK( lsmash_create_fragment_movie( p_mp4->p_root ) == 0,
                         "failed to create a movie fragment.\n" );
    }

    /* Append data per sample. */
    CHK( lsmash_append_sample( p_mp4->p_root, p_mp4->i_track, p_sample ) == 0,
                     "failed to append a video frame.\n" );

    p_mp4->i_prev_dts = dts;
    p_mp4->i_numframe++;

    return i_size;
}

static int encode_frame(x264_picture_t *pic) {
    x264_picture_t pic_out;
    x264_nal_t *nal;
    int i_nal;
    int i_frame_size = 0;
    
    i_frame_size = x264_encoder_encode(encoder_state.h, &nal, &i_nal, pic, &pic_out);
    
    if (i_frame_size) {
        mp4_write_frame(nal[0].p_payload, i_frame_size, &pic_out);
        encoder_state.last_dts = pic_out.i_dts;
        if (encoder_state.i_frames_written == 0) {        
            encoder_state.first_dts = pic_out.i_dts;
            encoder_state.largest_pts = encoder_state.second_largest_pts = pic_out.i_pts;
        } else {
            encoder_state.second_largest_pts = encoder_state.largest_pts;
            encoder_state.largest_pts = pic_out.i_pts;
        }
        encoder_state.i_frames_written++;
   }
    
    encoder_state.i_frame++;
    
    return i_frame_size > 0;
}


int CompressionSessionAddFrame(uint8_t *rgba) {
    int w = encoder_state.param.i_width;
    int h = encoder_state.param.i_height;
    
    uint8_t *yPtr = encoder_state.pic.img.plane[0];
    uint8_t *uPtr = encoder_state.pic.img.plane[1];
    uint8_t *vPtr = encoder_state.pic.img.plane[2];
    
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t px = (y * w * 4) + x * 4;
            uint8_t r = rgba[px];
            uint8_t g = rgba[px+1];
            uint8_t b = rgba[px+2];
            
            uint8_t yi = (16)+  ( 0.257*r+0.504*g+0.098*b);
            
            *yPtr++ = yi;
            if (y%2 == 0 && x%2 == 0) {
                uint8_t ui = (128)+ ( +0.439*r-0.368*g-0.071*b);
                uint8_t vi = (128)+ ( -0.148*r-0.291*g+0.439*b);
                *uPtr++ = ui;
                *vPtr++ = vi;
            }
        }
    }
    
    encoder_state.pic.i_pts = encoder_state.i_frame;
    return encode_frame(&encoder_state.pic);
}

static int mp4_close_file(int64_t largest_pts, int64_t second_largest_pts )
{
    mp4_state_t *p_mp4 = &mp4_state;

    if( !p_mp4 )
        return 0;

    if( p_mp4->p_root )
    {
        double actual_duration = 0;
        if( p_mp4->i_track )
        {
            /* Flush the rest of samples and add the last sample_delta. */
            int64_t last_delta = largest_pts - second_largest_pts;
            lsmash_flush_pooled_samples( p_mp4->p_root, p_mp4->i_track, (uint32_t)((last_delta ? last_delta : 1) * p_mp4->i_time_inc));

            if( p_mp4->i_movie_timescale != 0 && p_mp4->i_video_timescale != 0 )    /* avoid zero division */
                actual_duration = ((double)((largest_pts + last_delta) * p_mp4->i_time_inc) / p_mp4->i_video_timescale) * p_mp4->i_movie_timescale;
            else
                CHK(0, "timescale is broken.\n" );

            /*
             * Declare the explicit time-line mapping.
             * A segment_duration is given by movie timescale, while a media_time that is the start time of this segment
             * is given by not the movie timescale but rather the media timescale.
             * The reason is that ISO media have two time-lines, presentation and media time-line,
             * and an edit maps the presentation time-line to the media time-line.
             * According to QuickTime file format specification and the actual playback in QuickTime Player,
             * if the Edit Box doesn't exist in the track, the ratio of the summation of sample durations and track's duration becomes
             * the track's media_rate so that the entire media can be used by the track.
             * So, we add Edit Box here to avoid this implicit media_rate could distort track's presentation timestamps slightly.
             * Note: Any demuxers should follow the Edit List Box if it exists.
             */
            lsmash_edit_t edit;
            edit.duration   = actual_duration;
            edit.start_time = p_mp4->i_first_cts;
            edit.rate       = ISOM_EDIT_MODE_NORMAL;
            if( !p_mp4->b_fragments )
            {
                CHK( lsmash_create_explicit_timeline_map( p_mp4->p_root, p_mp4->i_track, edit ) == 0,
                                "failed to set timeline map for video.\n" );
            }
            else if( !p_mp4->b_stdout )
                CHK( lsmash_modify_explicit_timeline_map( p_mp4->p_root, p_mp4->i_track, 1, edit ) == 0,
                                "failed to update timeline map for video.\n" );
        }

        CHK( lsmash_finish_movie( p_mp4->p_root, NULL ) == 0, "failed to finish movie.\n" );
        
        lsmash_cleanup_summary( (lsmash_summary_t *)p_mp4->summary );
        lsmash_close_file( &p_mp4->file_param );
        lsmash_destroy_root( p_mp4->p_root );
        free( p_mp4->p_sei_buffer );
    }
    
    return 0;
}

int CompressionSessionFinish(void) {
    while (x264_encoder_delayed_frames(encoder_state.h)) {
        encode_frame(NULL);
    }
    
    x264_picture_clean(&encoder_state.pic);
    x264_encoder_close(encoder_state.h);
    mp4_close_file(encoder_state.largest_pts, encoder_state.second_largest_pts);
    
    return 0;
}

// usage: raw2mp4 output.mp4 width height image_001.raw image_002.raw ...
int main(int argc, char **argv) {
    size_t i = 1;
    char *output_path = argv[i++];
    int w = atoi(argv[i++]);
    int h = atoi(argv[i++]);
    
    printf("writing to %s, w=%d, h=%d\n", output_path, w, h);
    
    CompressionSessionOpen(output_path, w, h);
    size_t rgba_len = w * h * 4;
    uint8_t *rgba = malloc(rgba_len);
    for (; i < argc; i++) {
        FILE *infile = fopen(argv[i], "rb");
        size_t read = 0;
        if ((read = fread(rgba, 1, rgba_len, infile)) != rgba_len) {
            fprintf(stderr, "Short read (%zu) from %s\n", read, argv[i]);
            return 1; // short read
        }
        CompressionSessionAddFrame(rgba);
    }
    CompressionSessionFinish();
    
    printf("Finished!");

    return 0;
}

