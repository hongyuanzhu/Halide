open Bigarray

exception Unsupported_image_format

let load path =
  (* load the image *)
  let im = Images.load path [] in

  (* unpack metadata *)
  let (w,h) = Images.size im in

  (* convert down to rgb24 *)
  let rgb = match im with
    | Images.Index8 i -> Index8.to_rgb24 i
    | Images.Rgb24  i -> i
    | Images.Rgba32 i -> Rgb24.of_rgba32 i
    | _ -> raise Unsupported_image_format
  in

  (* extract raw bytes into 1D uint8 Bigarray *)
  let bigarray_of_string str =
    let arr = Array1.create int8_unsigned c_layout (String.length str) in
      for i=0 to (String.length str)-1 do
        arr.{i} <- Char.code str.[i]
      done;
      arr
  in

    (* NOTE: Rgb24.dump may raise out of memory for large images *)
    (w, h, bigarray_of_string(Rgb24.dump rgb))
