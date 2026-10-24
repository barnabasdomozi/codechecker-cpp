#ifndef CTU_API_H
#define CTU_API_H

// settings.cpp
struct Settings {
  int verbosity = 1;
};
// Sets 'out' to nullptr when 'key' is unknown.
void find_setting(Settings &settings, char key, int *&out);
int default_divisor();

// math.cpp
int scale(int value);
int local_scale(int value);

// pipeline_a.cpp -> pipeline_b.cpp -> pipeline_c.cpp
int stage_a(int value);
int stage_b(int *value);
int stage_c(int *value);

struct Counter {
  int hits = 0;
  int *slot = nullptr;
  void reset();
};

#endif
