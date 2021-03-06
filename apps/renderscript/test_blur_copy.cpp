#include "Halide.h"

using namespace Halide;

void blur_float(std::string suffix, ImageParam input8, const int channels) {
    Var x, y, c;
    Func input;
    input(x, y, c) = input8(clamp(x, input8.left(), input8.right()),
                            clamp(y, input8.top(), input8.bottom()), c);

    Func blur_x("blur_x");
    blur_x(x, y, c) = (input(x, y, c) +
        input(x + 1, y, c) +
        input(x + 2, y, c)) / 3;

    Func result("result");
    result(x, y, c) = (blur_x(x, y, c) +
        blur_x(x, y + 1, c) +
        blur_x(x, y + 2, c)) / 3;

    // Unset default constraints so that specialization works.
    result.output_buffer().set_stride(0, Expr());

    result.bound(c, 0, channels);

    Expr interleaved =
        (result.output_buffer().stride(0) == channels &&
         result.output_buffer().stride(2) == 1);
    Expr planar = result.output_buffer().stride(0) == 1;

    if (suffix == "_rs") {
        result.shader(x, y, c, DeviceAPI::Renderscript);
        result.specialize(interleaved).vectorize(c);
        // non-specialized version is planar
    } else {
        Var yi;
        result
            .reorder(c, x, y)
            .unroll(c)
            .split(y, y, yi, 32)
            .parallel(y)
            .vectorize(x, 4);
        result.specialize(interleaved);
        result.specialize(planar);
        // blur_x is compute at result, so it's included in result's
        // specializations.
        blur_x.store_at(result, y)
            .compute_at(result, yi)
            .reorder(c, x, y)
            .unroll(c)
            .vectorize(x, 4);
    }

    std::vector<Argument> args;
    args.push_back(input8);
    std::string fn_name = "generated_blur" + suffix + "_float";
    result.compile_to_file(fn_name, args, fn_name);
}

void copy_float(std::string suffix, ImageParam input8, const int channels) {
    Var x, y, c;
    Func input;
    input(x, y, c) = input8(clamp(x, input8.left(), input8.right()),
                            clamp(y, input8.top(), input8.bottom()), c);

    Func result("result");
    result(x, y, c) = input(x, y, c);
    result.bound(c, 0, channels);

    // Unset default constraints so that specialization works.
    result.output_buffer().set_stride(0, Expr());

    Expr interleaved =
        (result.output_buffer().stride(0) == channels &&
         result.output_buffer().stride(2) == 1);

    if (suffix == "_rs") {
        result.shader(x, y, c, DeviceAPI::Renderscript);
        result.specialize(interleaved).vectorize(c);
    } else {
        result.reorder(c, x, y)
            .parallel(y)
            .unroll(c)
            .vectorize(x, 4)
            .specialize(interleaved);
    }
    // non-specialized version is planar

    std::vector<Argument> args;
    args.push_back(input8);
    std::string fn_name = "generated_copy" + suffix + "_float";
    result.compile_to_file(fn_name, args, fn_name);
}

void blur_uint8(std::string suffix, ImageParam input8, const int channels) {
    Var x, y, c;
    Func input;
    input(x, y, c) = input8(clamp(x, input8.left(), input8.right()),
                            clamp(y, input8.top(), input8.bottom()), c);

    Func blur_x("blur_x");

    blur_x(x, y, c) = cast<uint8_t>(
        (cast<uint16_t>(input(x, y, c)) +
        input(x + 1, y, c) +
        input(x + 2, y, c)) / 3);

    Func result("result");
    result(x, y, c) = cast<uint8_t>(
        (cast<uint16_t>(blur_x(x, y, c)) +
        blur_x(x, y + 1, c) +
        blur_x(x, y + 2, c)) / 3);

    // Unset default constraints so that specialization works.
    result.output_buffer().set_stride(0, Expr());

    result.bound(c, 0, channels);

    Expr interleaved =
        (result.output_buffer().stride(0) == channels &&
         result.output_buffer().stride(2) == 1);
    Expr planar = result.output_buffer().stride(0) == 1;

    if (suffix == "_rs") {
        result.shader(x, y, c, DeviceAPI::Renderscript);
        result.specialize(interleaved).vectorize(c);
        // non-specialized version is planar
    } else {
        Var yi;
        result
            .reorder(c, x, y)
            .unroll(c)
            .split(y, y, yi, 32)
            .parallel(y)
            .vectorize(x, 8);
        result.specialize(interleaved);
        result.specialize(planar);
        // blur_x is compute at result, so it's included in result's
        // specializations.
        blur_x.store_at(result, y)
            .compute_at(result, yi)
            .reorder(c, x, y)
            .unroll(c)
            .vectorize(x, 8);
    }

    std::vector<Argument> args;
    args.push_back(input8);
    std::string fn_name = "generated_blur" + suffix + "_uint8";
    result.compile_to_file(fn_name, args, fn_name);
}

void copy_uint8(std::string suffix, ImageParam input8, const int channels) {
    Var x, y, c;
    Func input;
    input(x, y, c) = input8(clamp(x, input8.left(), input8.right()),
                            clamp(y, input8.top(), input8.bottom()), c);

    Func result("result");
    result(x, y, c) = input(x, y, c);
    result.bound(c, 0, channels);

    // Unset default constraints so that specialization works.
    result.output_buffer().set_stride(0, Expr());

    Expr interleaved =
        (result.output_buffer().stride(0) == channels &&
         result.output_buffer().stride(2) == 1);

    if (suffix == "_rs") {
        result.shader(x, y, c, DeviceAPI::Renderscript);
        result.specialize(interleaved).vectorize(c);
    } else {
        result.reorder(c, x, y)
            .parallel(y)
            .unroll(c)
            .vectorize(x, 16)
            .specialize(interleaved);
    }
    // non-specialized version is planar

    std::vector<Argument> args;
    args.push_back(input8);
    std::string fn_name = "generated_copy" + suffix + "_uint8";
    result.compile_to_file(fn_name, args, fn_name);
}

int main(int argc, char **argv) {
    const int channels = 4;

    ImageParam input_float(Float(32), 3, "input");
    input_float.set_stride(0, Expr());
    blur_float(argc > 1 ? argv[1] : "", input_float, channels);
    copy_float(argc > 1 ? argv[1]: "", input_float, channels);

    ImageParam input_uint8(UInt(8), 3, "input");
    input_uint8.set_stride(0, Expr());
    blur_uint8(argc > 1 ? argv[1] : "", input_uint8, channels);
    copy_uint8(argc > 1 ? argv[1]: "", input_uint8, channels);

    std::cout << "Done!" << std::endl;
}
