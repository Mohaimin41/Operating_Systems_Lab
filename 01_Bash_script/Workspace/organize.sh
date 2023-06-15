#!/bin/bash

# global state variables
verbose=0
noexecute=0

# global data variables
current_working_file_og_path=""
current_working_ext=""
current_roll=""
current_type=""
current_match=0
current_mismatch=0

# parameters
submissions="$1"
targets="$2"
tests="$3"
answers="$4"

# preset variables
c_filepath=../Match/"$targets"/C
py_filepath=../Match/"$targets"/Python
java_filepath=../Match/"$targets"/Java

reset_variables_dirs() {
    current_working_file_og_path=""
    current_roll=""
    current_working_ext=""
    current_match=0
    current_mismatch=0
    rm -rf temp/*
}

print_usage() {
    echo Usage:
    echo "./organize.sh <submission folder> <target folder> <test folder> <answer folder> [-v] [-noexecute]"
    echo
    echo -v: verbose
    echo -noexecute: do not execute code files
    echo
}

init_vars_dirs() {
    rm -rf ./temp ../Match
    mkdir ./temp
    mkdir -p ../Match/"$targets"/C ../Match/"$targets"/Java ../Match/"$targets"/Python
    touch ../Match/"$targets"/result.csv
    echo "student_id,type,matched,not_matched" >../Match/"$targets"/result.csv
}

extract_extension() {
    string="$1"
    extension=${string#*.}
    current_working_ext=$extension

    if [ $extension = "c" ]; then
        current_type="C"
    elif [ $extension = "py" ]; then
        current_type="Python"
    elif [ $extension = "java" ]; then
        current_type="Java"
    fi
}

extract_code_filepath() {
    filepath=$(find "$1" -path "*.py" -or -path "*.c" -or -path "*.java")
    extract_extension "$filepath"
    current_working_file_og_path="$filepath"
}

proecess_zipped_file() {
    string="$1"
    current_roll=${string#*}
    current_roll=${current_roll%.*}
    current_roll=${current_roll: -7}
    unzip -d ./temp -qq "$1"
}

mv_code_to_dir() {
    if [ "$current_working_ext" = "c" ]; then
        mkdir -p "$c_filepath"/$current_roll
        cp "$current_working_file_og_path" "$c_filepath"/$current_roll
        mv -n "$c_filepath"/$current_roll/*.c "$c_filepath"/$current_roll/main.c
    elif [ "$current_working_ext" = "py" ]; then
        mkdir -p "$py_filepath"/$current_roll
        cp "$current_working_file_og_path" "$py_filepath"/$current_roll
        mv -n "$py_filepath"/$current_roll/*.py "$py_filepath"/$current_roll/main.py
    elif [ "$current_working_ext" = "java" ]; then
        mkdir -p "$java_filepath"/$current_roll
        cp "$current_working_file_og_path" "$java_filepath"/$current_roll
        mv -n "$java_filepath"/$current_roll/*.java "$java_filepath"/$current_roll/Main.java
    fi
}

compile_curr_file() {
    if [ "$current_working_ext" = "c" ]; then
        gcc "$c_filepath"/$current_roll/main.c -o "$c_filepath"/$current_roll/main.out
    elif [ "$current_working_ext" = "java" ]; then
        javac "$java_filepath"/$current_roll/Main.java
    fi
}

run_tests() {
    for test in "$tests"/*; do
        serial=${test: -5:1}

        if [ $current_working_ext = "py" ]; then
            python3 "$py_filepath"/$current_roll/main.py <"$test" >"$py_filepath"/$current_roll/out${serial}.txt
        elif [ "$current_working_ext" = "c" ]; then
            "$c_filepath"/$current_roll/main.out <"$test" >"$c_filepath"/$current_roll/out${serial}.txt
        elif [ "$current_working_ext" = "java" ]; then
            java -cp "$java_filepath"/$current_roll/ Main <"$test" >"$java_filepath"/$current_roll/out${serial}.txt
        fi
    done
}

make_result() {
    for ans in "$answers"/*; do
        serial=${ans: -5:1}

        if [ $current_working_ext = "py" ]; then
            diff "$ans" "$py_filepath"/$current_roll/out${serial}.txt &>/dev/null
        elif [ "$current_working_ext" = "c" ]; then
            diff "$ans" "$c_filepath"/$current_roll/out${serial}.txt &>/dev/null
        elif [ "$current_working_ext" = "java" ]; then
            diff "$ans" "$java_filepath"/$current_roll/out${serial}.txt &>/dev/null
        fi

        if [ $? -eq 0 ]; then
            current_match=$((current_match + 1))
        else
            current_mismatch=$((current_mismatch + 1))
        fi
    done

    echo "$current_roll,$current_type,$current_match,$current_mismatch" >>../Match/"$targets"/result.csv
}

process_submissions() {
    for zipped_file in "$submissions"/*; do
        proecess_zipped_file "$zipped_file"
        extract_code_filepath temp
        if [ $verbose -eq 1 ]; then
            echo "Organizing files of $current_roll"
        fi
        mv_code_to_dir
        if [ $noexecute -eq 0 ]; then
            if [ $verbose -eq 1 ]; then
                echo "Executing files of $current_roll"
            fi
            compile_curr_file
            run_tests
            make_result
        fi
        reset_variables_dirs
    done
}

main() {
    init_vars_dirs "$targets"
    test_file_count=$(ls "$tests" | wc -l)
    if [ $verbose -eq 1 ]; then
        echo "Found "$test_file_count" test files"
    fi
    process_submissions
    rm -rf temp
}

program() {

    if ! [ -d $1 ] || ! [ -d $3 ] || ! [ -d $4 ]; then
        print_usage
        return 1
    fi
    if [ $# -lt 4 ] || [ $# -gt 6 ]; then
        print_usage
        return 2
    elif [ $# -eq 4 ]; then
        main
        return 0
    elif [ $# -eq 5 ]; then
        if [ $5 = "-v" ]; then
            verbose=1
        elif [ $5 = "-noexecute" ]; then
            noexecute=1
        fi
        main
        return 0
    elif [ $# -eq 6 ]; then
        if
           { [ $5 = "-v" ] && [ $6 = "-noexecute" ]; } ||
               { [ $6 = "-v" ] && [ $5 = "-noexecute" ]; }
        then
            verbose=1
            noexecute=1
            main
            return 0
        else
            print_usage
            return 3
        fi
    fi

}

program $*
