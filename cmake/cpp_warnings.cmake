# This module defines a list of common warnings for the project called PROJECT_WARNING_FLAGS.
# By default this set includes as many reasonable warnings as possible. Only warnings that are supported by the compiler
# version in use will be enabled. The list of warnings is meant to be adapted and tweaked as needed for each project.
# For more details see https://lefticus.gitbooks.io/cpp-best-practices/content/02-Use_the_Tools_Available.html

include_guard(GLOBAL)

message(STATUS "Configuring PROJECT_WARNING_FLAGS.")
# Generate a warning flags list.
set(PROJECT_WARNING_FLAGS)

# Warnings present on all supported versions of GCC and Clang.
list(APPEND PROJECT_WARNING_FLAGS
    -Wall                # Enables most warnings.
    -Wextra              # Enables an extra set of warnings.
    -pedantic            # Strict compliance to the standard is not met.
    -Wcast-align         # Pointer casts which increase alignment.
    -Wcast-qual          # A pointer is cast to remove a type qualifier, or add an unsafe one.
    -Wconversion         # Implicit type conversions that may change a value.
    -Wformat=2           # printf/scanf/strftime/strfmon format string anomalies.
    -Wnon-virtual-dtor   # Non-virtual destructors are found.
    -Wold-style-cast     # C-style cast is used in a program.
    -Woverloaded-virtual # Overloaded virtual function names.
    -Wsign-conversion    # Implicit conversions between signed and unsigned integers.
    -Wshadow             # One variable shadows another.
    -Wswitch             # A switch statement has an index of enumerated type and lacks a case.
    -Wundef              # An undefined identifier is evaluated in an #if directive.
    -Wunused             # Enable all -Wunused- warnings.
    -Wunused-result      # Warn when a result of a function is ignored (for [[nodiscard]]).
    -Werror              # Warnings as errors
)

# Enable additional warnings depending on the compiler and compiler version in use.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND PROJECT_WARNING_FLAGS
        -Wdisabled-optimization       # GCC’s optimizers are unable to handle the code effectively.
        -Weffc++                      # Warnings related to guidelines from Scott Meyers’ Effective C++ books.
        -Wlogical-op                  # Warn when a logical operator is always evaluating to true or false.
        -Wsign-promo                  # Overload resolution chooses a promotion from unsigned to a signed type.
        -Wswitch-default              # A switch statement does not have a default case.
        -Wredundant-decls             # Something is declared more than once in the same scope.
    )
    if (NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.6)
        list(APPEND PROJECT_WARNING_FLAGS
            -Wdouble-promotion          # Warn about implicit conversions from "float" to "double".
        )
    endif ()
    if (NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        list(APPEND PROJECT_WARNING_FLAGS
            -Wuseless-cast              # Warn about useless casts.
        )
    endif ()
    if (NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5))
        list(APPEND PROJECT_WARNING_FLAGS
            -Wdate-time                 # Warn when encountering macros that might prevent bit-wise-identical compilations.
            -Wsuggest-final-methods     # Virtual methods that could be declared final or in an anonymous namespace.
            -Wsuggest-final-types       # Types with virtual methods that can be declared final or in an anonymous namespace.
            -Wsuggest-override          # Overriding virtual functions that are not marked with the override keyword.
        )
    endif ()
    if (NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 6))
        list(APPEND PROJECT_WARNING_FLAGS
            -Wduplicated-cond           # Warn about duplicated conditions in an if-else-if chain.
            -Wmisleading-indentation    # Warn when indentation does not reflect the block structure.
            -Wmultiple-inheritance      # Do not allow multiple inheritance.
            -Wnull-dereference          # Dereferencing a pointer may lead to undefined behavior.
        )
    endif ()
    if (NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7)
        list(APPEND PROJECT_WARNING_FLAGS
            -Walloca                    # Warn on any usage of alloca in the code.
            -Wduplicated-branches       # Warn about duplicated branches in if-else statements.
        )
    endif ()
    if (NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8)
        list(APPEND PROJECT_WARNING_FLAGS
            -Wextra-semi                # Redundant semicolons after in-class function definitions.
            -Wunsafe-loop-optimizations # The loop cannot be optimized because the compiler cannot assume anything.
        )
    endif ()
    if (NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
        list(APPEND PROJECT_WARNING_FLAGS
            -Warith-conversion          # Stricter implicit conversion warnings in arithmetic operations.
            -Wredundant-tags            # Redundant class-key and enum-key where it can be eliminated.
        )
    endif ()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND PROJECT_WARNING_FLAGS
        -Weverything                    # Enables every Clang warning.
        -Wno-c++98-compat               # This project is not compatible with C++98.
        -Wno-c++98-compat-pedantic      # This project is not compatible with C++98.
        -Wdouble-promotion              # Warn about implicit conversions from "float" to "double".
        -Wnull-dereference              # Dereferencing a pointer may lead to erroneous or undefined behavior.
        -Wno-unknown-warning-option     # Ignore unknown warning options.
    )
endif ()
