#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <map>
#include <vector>
#include <string>
#include <queue>
#include <iostream>
#include <algorithm>
#ifdef _MSC_VER
#include <io.h>
typedef int64_t ssize_t;
#else
#include <unistd.h>
#endif
#include <string.h>

#include "inconsolata.h"

namespace {

using std::map;
using std::vector;
using std::string;
using std::queue;

// The first 48 bytes of a tracing packet are metadata
const int packet_header_size = 48;

// A struct representing a single Halide tracing packet.
struct Packet {
    uint32_t id, parent;
    uint8_t event, type, bits, width, value_idx, num_int_args;
    char name[packet_header_size - 14];
    uint8_t payload[4096 - packet_header_size]; // Not all of this will be used, but this is the max possible packet size.

    size_t value_bytes() const {
        size_t bytes_per_elem = 1;
        while (bytes_per_elem*8 < bits) bytes_per_elem <<= 1;
        return bytes_per_elem * width;
    }

    size_t int_args_bytes() const {
        return sizeof(int) * num_int_args;
    }

    size_t payload_bytes() const {
        return value_bytes() + int_args_bytes();
    }

    int get_int_arg(int idx) const {
        return ((const int *)(payload + value_bytes()))[idx];
    }

    template<typename T>
    T get_value_as(int idx) const {
        switch (type) {
        case 0: // int
            switch (bits) {
            case 8:
                return (T)(((const int8_t *)payload)[idx]);
            case 16:
                return (T)(((const int16_t *)payload)[idx]);
            case 32:
                return (T)(((const int32_t *)payload)[idx]);
            case 64:
                return (T)(((const int64_t *)payload)[idx]);
            default:
                bad_type_error();
            }
            break;
        case 1: // uint
            switch (bits) {
            case 8:
                return (T)(((const uint8_t *)payload)[idx]);
            case 16:
                return (T)(((const uint16_t *)payload)[idx]);
            case 32:
                return (T)(((const uint32_t *)payload)[idx]);
            case 64:
                return (T)(((const uint64_t *)payload)[idx]);
            default:
                bad_type_error();
            }
            break;
        case 2: // float
            switch (bits) {
            case 32:
                return (T)(((const float *)payload)[idx]);
            case 64:
                return (T)(((const double *)payload)[idx]);
            default:
                bad_type_error();
            }
            break;
        default:
            bad_type_error();
        }
        return (T)0;
    }

    // Grab a packet from stdin. Returns false when stdin closes.
    bool read_from_stdin() {
        if (!read_stdin(this, packet_header_size)) {
            return false;
        }
        if (!read_stdin(payload, payload_bytes())) {
            fprintf(stderr, "Unexpected EOF mid-packet");
        }
        name[sizeof(name)-1] = 0;
        return true;
    }

private:
    // Do a blocking read of some number of bytes from stdin.
    bool read_stdin(void *d, ssize_t size) {
        uint8_t *dst = (uint8_t *)d;
        if (!size) return true;
        for (;;) {
            ssize_t s = read(0, dst, size);
            if (s == 0) {
                // EOF
                return false;
            } else if (s < 0) {
                perror("Failed during read");
                exit(-1);
                return 0;
            } else if (s == size) {
                return true;
            }
            size -= s;
            dst += s;
        }
    }

    void bad_type_error() const {
        fprintf(stderr, "Can't visualize packet with type: %d bits: %d\n", type, bits);
    }
};

// A struct specifying a text label that will appear on the screen at some point.
struct Label {
    const char *text;
    int x, y, n;
};

// A struct specifying how a single Func will get visualized.
struct FuncInfo {
    // Configuration for how the func should be drawn
    struct {
        int zoom = 0;
        int cost = 0;
        int dims = 0;
        int x, y = 0;
        int x_stride[16] {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        int y_stride[16] {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        int color_dim = 0;
        float min = 0.0f, max = 0.0f;
        vector<Label> labels;
        bool blank_on_end_realization = false;

        void dump(const char *name) {
            fprintf(stderr,
                    "Func %s:\n"
                    " min: %f max: %f\n"
                    " color_dim: %d\n"
                    " blank: %d\n"
                    " dims: %d\n"
                    " zoom: %d\n"
                    " cost: %d\n"
                    " x: %d y: %d\n"
                    " x_stride: %d %d %d %d\n"
                    " y_stride: %d %d %d %d\n",
                    name,
                    min, max,
                    color_dim,
                    blank_on_end_realization,
                    dims,
                    zoom, cost, x, y,
                    x_stride[0], x_stride[1], x_stride[2], x_stride[3],
                    y_stride[0], y_stride[1], y_stride[2], y_stride[3]);
        }
    } config;

    // Information about actual observed values gathered while parsing the trace
    struct {
        string qualified_name;
        int first_draw_time = 0, first_packet_idx = 0;
        double min_value = 0.0, max_value = 0.0;
        int min_coord[16] {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        int max_coord[16] {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        int num_realizations = 0, num_productions = 0;
        uint64_t stores = 0, loads = 0;

        void observe_load(const Packet &p) {
            observe_load_or_store(p);
            loads += p.width;
        }

        void observe_store(const Packet &p) {
            observe_load_or_store(p);
            stores += p.width;
        }

        void observe_load_or_store(const Packet &p) {
            for (int i = 0; i < std::min(16, p.num_int_args / p.width); i++) {
                for (int lane = 0; lane < p.width; lane++) {
                    int coord = p.get_int_arg(i*p.width + lane);
                    if (loads + stores == 0 && lane == 0) {
                        min_coord[i] = coord;
                        max_coord[i] = coord + 1;
                    } else {
                        min_coord[i] = std::min(min_coord[i], coord);
                        max_coord[i] = std::max(max_coord[i], coord + 1);
                    }
                }
            }

            for (int i = 0; i < p.width; i++) {
                double value = p.get_value_as<double>(i);
                if (stores + loads == 0) {
                    min_value = value;
                    max_value = value;
                } else {
                    min_value = std::min(min_value, value);
                    max_value = std::max(max_value, value);
                }
            }
        }

        void report() {
            fprintf(stderr,
                    "Func %s:\n"
                    " bounds of domain: ", qualified_name.c_str());
            for (int i = 0; i < 16; i++) {
                if (min_coord[i] == 0 && max_coord[i] == 0) break;
                if (i > 0) {
                    fprintf(stderr, " x ");
                }
                fprintf(stderr, "[%d, %d)", min_coord[i], max_coord[i]);
            }
            fprintf(stderr,
                    "\n"
                    " range of values: [%f, %f]\n"
                    " number of realizations: %d\n"
                    " number of productions: %d\n"
                    " number of loads: %llu\n"
                    " number of stores: %llu\n",
                    min_value, max_value,
                    num_realizations, num_productions,
                    (long long unsigned)loads,
                    (long long unsigned)stores);
        }

    } stats;
};

// Composite a single pixel of b over a single pixel of a, writing the result into dst
void composite(uint8_t *a, uint8_t *b, uint8_t *dst) {
    uint8_t alpha = b[3];
    dst[0] = (alpha * b[0] + (256 - alpha) * a[0]) >> 8;
    dst[1] = (alpha * b[1] + (256 - alpha) * a[1]) >> 8;
    dst[2] = (alpha * b[2] + (256 - alpha) * a[2]) >> 8;
    dst[3] = 0xff;
}

#define FONT_W 12
#define FONT_H 32
void draw_text(const char *text, int x, int y, uint32_t color, uint32_t *dst, int dst_width, int dst_height) {
    // The font array contains 96 characters of FONT_W * FONT_H letters.
    assert(inconsolata_raw_len == 96 * FONT_W * FONT_H);

    // Drop any alpha component of color
    color &= 0xffffff;

    for (int c = 0; ; c++) {
        int chr = text[c];
        if (chr == 0) return;

        // We only handle a subset of ascii
        if (chr < 32 || chr > 127) chr = 32;
        chr -= 32;

        uint8_t *font_ptr = inconsolata_raw + chr * (FONT_W * FONT_H);
        for (int fy = 0; fy < FONT_H; fy++) {
            for (int fx = 0; fx < FONT_W; fx++) {
                int px = x + FONT_W*c + fx;
                int py = y - FONT_H + fy + 1;
                if (px < 0 || px >= dst_width ||
                    py < 0 || py >= dst_height) continue;
                dst[py * dst_width + px] = (font_ptr[fy * FONT_W + fx] << 24) | color;
            }
        }
    }
}

void usage() {
    fprintf(stderr,
            "\n"
            "HalideTraceViz accepts Halide-generated binary tracing packets from\n"
            "stdin, and outputs them as raw 8-bit rgba32 pixel values to\n"
            "stdout. You should pipe the output of HalideTraceViz into a video\n"
            "encoder or player.\n"
            "\n"
            "E.g. to encode a video:\n"
            " HL_TRACE=3 <command to make pipeline> && \\\n"
            " HL_TRACE_FILE=/dev/stdout <command to run pipeline> | \\\n"
            " HalideTraceViz -s 1920 1080 -t 10000 <the -f args> | \\\n"
            " avconv -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 output.avi\n"
            "\n"
            "To just watch the trace instead of encoding a video replace the last\n"
            "line with something like:\n"
            " mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -\n"
            "\n"
            "The arguments to HalideTraceViz are: \n"
            " -s width height: The size of the output frames. Defaults to 1920 x 1080.\n"
            "\n"
            " -t timestep: How many Halide computations should be covered by each\n"
            "    frame. Defaults to 10000.\n"
            " -d decay factor: How quickly should the yellow and blue highlights \n"
            "    decay over time\n"
            " -h hold frames: How many frames to output after the end of the trace. \n"
            "    Defaults to 250.\n"
            " -l func label x y n: When func is first touched, the label appears at\n"
            "    the given coordinates and fades in over n frames.\n"
            "\n"
            " For each Func you want to visualize, also specify:\n"
            " -f func_name min_value max_value color_dim blank zoom cost x y strides\n"
            " where\n"
            "  func_name: The name of the func or input image. If you have multiple \n"
            "    pipelines that use Funcs or the same name, you can optionally \n"
            "    prefix this with the name of the containing pipeline like so: \n"
            "    pipeline_name:func_name\n"
            "\n"
            "  min_value: The minimum value taken on by the Func. Values less than\n"
            "    or equal to this will map to black\n"
            "\n"
            "  max_value: The maximum value taken on by the Func. Values greater\n"
            "    than or equal to this will map to white\n"
            "\n"
            "  color_dim: Which dimension of the Func corresponds to color\n"
            "    channels. Usually 2. Set it to -1 if you want to visualize the Func\n"
            "    as grayscale\n"
            "\n"
            "  blank: Should the output occupied by the Func be set to black on end\n"
            "    realization events. Zero for no, one for yes.\n"
            "\n"
            "  zoom: Each value of the Func will draw as a zoom x zoom box in the output\n"
            "\n"
            "  cost: How much time in the output video should storing one value to\n"
            "    this Func take. Relative to the timestep.\n"
            "\n"
            "  x, y: The position on the screen corresponding to the Func's 0, 0\n"
            "    coordinate.\n"
            "\n"
            "  strides: A matrix that maps the coordinates of the Func to screen\n"
            "    pixels. Specified column major. For example, 1 0 0 1 0 0\n"
            "    specifies that the Func has three dimensions where the\n"
            "    first one maps to screen-space x coordinates, the second\n"
            "    one maps to screen-space y coordinates, and the third one\n"
            "    does not affect screen-space coordinates.\n"
        );

}

int run(int argc, char **argv) {
    static_assert(sizeof(Packet) == 4096, "");

    // State that determines how different funcs get drawn
    int frame_width = 1920, frame_height = 1080;
    int decay_factor = 2;
    map<string, FuncInfo> func_info;

    int timestep = 10000;
    int hold_frames = 250;

    // Parse command line args
    int i = 1;
    while (i < argc) {
        string next = argv[i];
        if (next == "-s") {
            if (i + 2 >= argc) {
                usage();
                return -1;
            }
            frame_width = atoi(argv[++i]);
            frame_height = atoi(argv[++i]);
        } else if (next == "-f") {
            if (i + 9 >= argc) {
                usage();
                return -1;
            }
            char *func = argv[++i];
            FuncInfo &fi = func_info[func];
            fi.config.min = atof(argv[++i]);
            fi.config.max = atof(argv[++i]);
            fi.config.color_dim = atoi(argv[++i]);
            fi.config.blank_on_end_realization = atoi(argv[++i]);
            fi.config.zoom = atoi(argv[++i]);
            fi.config.cost = atoi(argv[++i]);
            fi.config.x = atoi(argv[++i]);
            fi.config.y = atoi(argv[++i]);
            int d = 0;
            for (; i+1 < argc && argv[i+1][0] != '-' && d < 16; d++) {
                fi.config.x_stride[d] = atoi(argv[++i]);
                fi.config.y_stride[d] = atoi(argv[++i]);
            }
            fi.config.dims = d;
            fi.config.dump(func);

        } else if (next == "-l") {
            if (i + 5 >= argc) {
                usage();
                return -1;
            }
            char *func = argv[++i];
            char *text = argv[++i];
            int x = atoi(argv[++i]);
            int y = atoi(argv[++i]);
            int n = atoi(argv[++i]);
            Label l = {text, x, y, n};
            fprintf(stderr, "Adding label %s to func %s\n",
                    text, func);
            func_info[func].config.labels.push_back(l);
        } else if (next == "-t") {
            if (i + 1 >= argc) {
                usage();
                return -1;
            }
            assert(i + 1 < argc);
            timestep = atoi(argv[++i]);
        } else if (next == "-d") {
            if (i + 1 >= argc) {
                usage();
                return -1;
            }
            assert(i + 1 < argc);
            decay_factor = atoi(argv[++i]);
        } else if (next == "-h") {
            if (i + 1 >= argc) {
                usage();
                return -1;
            }
            assert(i + 1 < argc);
            hold_frames = atoi(argv[++i]);
        } else {
            usage();
            return -1;
        }
        i++;
    }

    // halide_clock counts halide events. video_clock counts how many
    // of these events have been output. When halide_clock gets ahead
    // of video_clock, we emit a new frame.
    size_t halide_clock = 0, video_clock = 0;

    // There are three layers - image data, an animation on top of
    // it, and text labels. These layers get composited.
    uint32_t *image = new uint32_t[frame_width * frame_height];
    memset(image, 0, 4 * frame_width * frame_height);

    uint32_t *anim = new uint32_t[frame_width * frame_height];
    memset(anim, 0, 4 * frame_width * frame_height);

    uint32_t *text = new uint32_t[frame_width * frame_height];
    memset(text, 0, 4 * frame_width * frame_height);

    uint32_t *blend = new uint32_t[frame_width * frame_height];
    memset(blend, 0, 4 * frame_width * frame_height);

    struct PipelineInfo {
        string name;
        uint32_t id;
    };

    map<uint32_t, PipelineInfo> pipeline_info;

    size_t end_counter = 0;
    size_t packet_clock = 0;
    for (;;) {
        // Hold for some number of frames once the trace has finished.
        if (end_counter) {
            halide_clock += timestep;
            if (end_counter == (size_t)hold_frames) {
                break;
            }
        }

        while (halide_clock >= video_clock) {
            // Composite text over anim over image
            for (int i = 0; i < frame_width * frame_height; i++) {
                uint8_t *anim_px  = (uint8_t *)(anim + i);
                uint8_t *image_px = (uint8_t *)(image + i);
                uint8_t *text_px  = (uint8_t *)(text + i);
                uint8_t *blend_px = (uint8_t *)(blend + i);
                composite(image_px, anim_px, blend_px);
                composite(blend_px, text_px, blend_px);
            }

            // Dump the frame
            ssize_t bytes = 4 * frame_width * frame_height;
            ssize_t bytes_written = write(1, blend, bytes);
            if (bytes_written < bytes) {
                fprintf(stderr, "Could not write frame to stdout.\n");
                return -1;
            }

            video_clock += timestep;

            // Decay the alpha channel on the anim
            for (int i = 0; i < frame_width * frame_height; i++) {
                uint32_t color = anim[i];
                uint32_t rgb = color & 0x00ffffff;
                uint8_t alpha = (color >> 24);
                alpha /= decay_factor;
                anim[i] = (alpha << 24) | rgb;
            }
        }

        // Read a tracing packet
        Packet p;
        if (!p.read_from_stdin()) {
            end_counter++;
            continue;
        }
        packet_clock++;

        // It's a pipeline begin/end event
        if (p.event == 8) {
            pipeline_info[p.id] = {p.name, p.id};
            continue;
        } else if (p.event == 9) {
            pipeline_info.erase(p.id);
            continue;
        }

        PipelineInfo pipeline = pipeline_info[p.parent];

        string qualified_name = pipeline.name + ":" + p.name;
        string search_name = qualified_name;

        // First look it up as fully qualified.
        if (func_info.find(search_name) == func_info.end()) {
            search_name = p.name;
        }
        // Then try the unqualified func name
        if (func_info.find(search_name) == func_info.end()) {
            fprintf(stderr, "Warning: ignoring func %s\n", qualified_name.c_str());
        }

        // Draw the event
        FuncInfo &fi = func_info[search_name];

        if (fi.stats.first_draw_time == 0) {
            fi.stats.first_draw_time = halide_clock;
        }

        if (fi.stats.first_packet_idx == 0) {
            fi.stats.first_packet_idx = packet_clock;
            fi.stats.qualified_name = qualified_name;
        }

        switch (p.event) {
        case 0: // load
            fi.stats.observe_load(p);
        case 1: // store
        {
            int frames_since_first_draw = (halide_clock - fi.stats.first_draw_time) / timestep;

            for (size_t i = 0; i < fi.config.labels.size(); i++) {
                const Label &label = fi.config.labels[i];
                if (frames_since_first_draw <= label.n) {
                    uint32_t color = ((1 + frames_since_first_draw) * 255) / label.n;
                    if (color > 255) color = 255;
                    color *= 0x10101;

                    draw_text(label.text, label.x, label.y, color, text, frame_width, frame_height);
                }
            }

            if (p.event == 1) {
                // Stores take time proportional to the number of
                // items stored times the cost of the func.
                halide_clock += fi.config.cost * (p.value_bytes() / (p.bits / 8));

                fi.stats.observe_store(p);
            }
            // Check the tracing packet contained enough information
            // given the number of dimensions the user claims this
            // Func has.
            assert(p.num_int_args >= p.width * fi.config.dims);
            if (p.num_int_args >= p.width * fi.config.dims) {
                for (int lane = 0; lane < p.width; lane++) {
                    // Compute the screen-space x, y coord to draw this.
                    int x = fi.config.x;
                    int y = fi.config.y;
                    for (int d = 0; d < fi.config.dims; d++) {
                        int a = p.get_int_arg(d * p.width + lane);
                        x += fi.config.zoom * fi.config.x_stride[d] * a;
                        y += fi.config.zoom * fi.config.y_stride[d] * a;
                    }

                    // Stores are orange, loads are blue.
                    uint32_t color = p.event == 0 ? 0xffffdd44 : 0xff44ddff;

                    uint32_t image_color;
                    bool update_image = false;

                    // If it's a store, or a load from an input,
                    // update one or more of the color channels of the
                    // image layer.

                    if (p.event == 1 || p.parent == pipeline.id) {
                        update_image = true;
                        // Get the old color, in case we're only
                        // updating one of the color channels.
                        image_color = image[frame_width * y + x];

                        double value = p.get_value_as<double>(lane);

                        // Normalize it.
                        value = 255 * (value - fi.config.min) / (fi.config.max - fi.config.min);
                        if (value < 0) value = 0;
                        if (value > 255) value = 255;

                        // Convert to 8-bit color.
                        uint8_t int_value = (uint8_t)value;

                        if (fi.config.color_dim < 0) {
                            // Grayscale
                            image_color = (int_value * 0x00010101) | 0xff000000;
                        } else {
                            // Color
                            uint32_t channel = p.get_int_arg(fi.config.color_dim * p.width + lane);
                            uint32_t mask = ~(255 << (channel * 8));
                            image_color &= mask;
                            image_color |= int_value << (channel * 8);
                        }
                    }

                    // Draw the pixel
                    for (int dy = 0; dy < fi.config.zoom; dy++) {
                        for (int dx = 0; dx < fi.config.zoom; dx++) {
                            if (y + dy >= 0 && y + dy < frame_height &&
                                x + dx >= 0 && x + dx < frame_width) {
                                int px = frame_width * (y + dy) + x + dx;
                                anim[px] = color;
                                if (update_image) {
                                    image[px] = image_color;
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case 2: // begin realization
            fi.stats.num_realizations++;
            pipeline_info[p.id] = pipeline;
            break;
        case 3: // end realization
            if (fi.config.blank_on_end_realization) {
                assert(p.num_int_args >= 2 * fi.config.dims);
                int x_min = fi.config.x, y_min = fi.config.y;
                int x_extent = 0, y_extent = 0;
                for (int d = 0; d < fi.config.dims; d++) {
                    int m = p.get_int_arg(d * 2 + 0);
                    int e = p.get_int_arg(d * 2 + 1);
                    x_min += fi.config.zoom * fi.config.x_stride[d] * m;
                    y_min += fi.config.zoom * fi.config.y_stride[d] * m;
                    x_extent += fi.config.zoom * fi.config.x_stride[d] * e;
                    y_extent += fi.config.zoom * fi.config.y_stride[d] * e;
                }
                if (x_extent == 0) x_extent = fi.config.zoom;
                if (y_extent == 0) y_extent = fi.config.zoom;
                for (int y = y_min; y < y_min + y_extent; y++) {
                    for (int x = x_min; x < x_min + x_extent; x++) {
                        image[y * frame_width + x] = 0;
                    }
                }
            }
            pipeline_info.erase(p.parent);
            break;
        case 4: // produce
            pipeline_info[p.id] = pipeline;
            fi.stats.num_productions++;
            break;
        case 5: // update
            break;
        case 6: // consume
            break;
        case 7: // end consume
            pipeline_info.erase(p.parent);
            break;
        default:
            fprintf(stderr, "Unknown tracing event code: %d\n", p.event);
            exit(-1);
        }

    }

    fprintf(stderr, "Total number of Funcs: %d\n", (int)func_info.size());

    // Print stats about the Func gleaned from the trace.
    vector<std::pair<std::string, FuncInfo> > funcs;
    for (std::pair<std::string, FuncInfo> p : func_info) {
        funcs.push_back(p);
    }
    struct by_first_packet_idx {
        bool operator()(const std::pair<std::string, FuncInfo> &a,
                        const std::pair<std::string, FuncInfo> &b) const {
            return a.second.stats.first_packet_idx < b.second.stats.first_packet_idx;
        }
    };
    std::sort(funcs.begin(), funcs.end(), by_first_packet_idx());
    for (std::pair<std::string, FuncInfo> p : funcs) {
        p.second.stats.report();
    }

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    run(argc, argv);
}
