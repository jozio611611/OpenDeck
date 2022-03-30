#!/usr/bin/env bash

if [[ ! -f compile_commands.json ]]
then
    echo "compile_commands.json file not found"
    exit 1
fi

for arg in "$@"; do
    case "$arg" in
        --compiler=*)
            compiler=${arg#--compiler=}
            ;;

        --output=*)
            output_file=${arg#--output=}
            ;;

        --option-files-dir=*)
            option_files_dir=${arg#--option-files-dir=}
            ;;

    esac
done

if [[ -z $output_file ]]
then
  echo "Output file not specified"
  exit 1
fi

extra_args=()

case $compiler in
  arm-none-eabi-gcc)
    toolchain_dir=$(dirname "$(which arm-none-eabi-gcc)" | rev | cut -d/ -f2- | rev)
    toolchain_ver=$(find "$toolchain_dir"/arm-none-eabi/include/c++ -mindepth 1 -maxdepth 1 | rev | cut -d/ -f 1 | rev)
    extra_args+=(-extra-arg="-I$toolchain_dir/arm-none-eabi/include/c++/$toolchain_ver")
    extra_args+=(-extra-arg="-I$toolchain_dir/arm-none-eabi/include/c++/$toolchain_ver/arm-none-eabi")
    extra_args+=(-extra-arg="-I$toolchain_dir/arm-none-eabi/include/c++/$toolchain_ver/backward")
    extra_args+=(-extra-arg="-I$toolchain_dir/lib/gcc/arm-none-eabi/$toolchain_ver/include")
    extra_args+=(-extra-arg="-I$toolchain_dir/lib/gcc/arm-none-eabi/$toolchain_ver/include-fixed")
    extra_args+=(-extra-arg="-I$toolchain_dir/arm-none-eabi/sys-include")
    extra_args+=(-extra-arg="-I$toolchain_dir/arm-none-eabi/include")
    extra_args+=(-extra-arg="-Wno-unknown-attributes")
    ;;

  avr-gcc)
    toolchain_dir=$(dirname "$(which avr-gcc)" | rev | cut -d/ -f2- | rev)
    extra_args+=(-extra-arg="-I$toolchain_dir/avr/include/avr")
    extra_args+=(-extra-arg="-I$toolchain_dir/avr/include")
    extra_args+=(-extra-arg="-I$toolchain_dir/lib/gcc/avr/7.3.0/include")
    extra_args+=(-extra-arg="-Wno-unknown-attributes")
    extra_args+=(-extra-arg="--target=avr")
    ;;

  *)
    # nothing to add
    ;;
esac

if [[ -n $option_files_dir ]]
then
  readarray -t ignore_paths <"$option_files_dir"/.clang-tidy-ignore
  ignore_regex="^((?!"

  for line in "${ignore_paths[@]}"
  do
    ignore_regex+=$line
    ignore_regex+="|"
  done

  #last '|' must be removed
  ignore_regex=${ignore_regex::-1}
  ignore_regex+=").)*$"

  # clean up compile_commands.json
  readarray -t cleanup_args <"$option_files_dir"/.clang-tidy-compile-commands-clean

  for line in "${cleanup_args[@]}"
  do
      sed -i "s/$line//g" compile_commands.json
  done
fi

run-clang-tidy \
-style=file \
-fix \
-format \
"${extra_args[@]}" \
-export-fixes "$output_file" \
"$ignore_regex"

exit 0