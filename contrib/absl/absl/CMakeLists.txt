ADD_LIBRARY (absl STATIC
    numeric/int128.cc
)

ADD_LIBRARY (absl::absl ALIAS absl)
