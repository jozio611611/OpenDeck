#!/usr/bin/env bash

for arg in "$@"; do
    case "$arg" in
        --project=*)
            project=${arg#--project=}
            ;;

        --target-config=*)
            yaml_file=${arg#--target-config=}
            ;;

        --gen-dir-target=*)
            gen_dir=${arg#--gen-dir-target=}
            ;;

        --base-gen-dir-mcu=*)
            base_mcu_gen_dir=${arg#--base-gen-dir-mcu=}
            ;;
    esac
done

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
yaml_parser="dasel -n -p yaml --plain -f"
out_header="$gen_dir"/Target.h
out_makefile="$gen_dir"/Makefile
mcu=$($yaml_parser "$yaml_file" mcu)
target_name=$(basename "$yaml_file" .yml)
mcu_gen_dir=$base_mcu_gen_dir/$mcu
hw_test_yaml_file=$(dirname "$yaml_file")/../hw-test/$target_name.yml
extClockMhz=$($yaml_parser "$yaml_file" extClockMhz)

mkdir -p "$gen_dir"
echo "" > "$out_header"
echo "" > "$out_makefile"

if [[ ! -d $mcu_gen_dir ]]
then
    if ! "$script_dir"/gen_mcu.sh "$mcu" "$mcu_gen_dir" "$extClockMhz"
    then
        exit 1
    fi
fi

echo "Generating target definitions..."

source "$script_dir"/target/main.sh

if [[ -f $hw_test_yaml_file ]]
then
    # HW config files go into the same dir as target ones
    if ! "$script_dir"/gen_hwconfig.sh "$project" "$hw_test_yaml_file" "$gen_dir"
    then
        exit 1
    else
        printf "%s\n" "PROJECT_TARGET_DEFINES += PROJECT_TARGET_SUPPORT_HW_TESTS" >> "$out_makefile"
    fi
fi