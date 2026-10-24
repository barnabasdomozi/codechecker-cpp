// Every function has one division by zero (core.DivideZero). The comment
// above the bug decides its review status.

int no_comment(int a) {
  int z = 0;
  return a / z;
}

int suppress_marker(int a) {
  int z = 0;
  // codechecker_suppress [core.DivideZero] legacy marker, same as false positive
  return a / z;
}

int false_positive_marker(int a) {
  int z = 0;
  // codechecker_false_positive [core.DivideZero] z is never zero in practice
  return a / z;
}

int intentional_marker(int a) {
  int z = 0;
  // codechecker_intentional [core.DivideZero] the crash is the point
  return a / z;
}

int confirmed_marker(int a) {
  int z = 0;
  // codechecker_confirmed [core.DivideZero] real bug, see ticket 42
  return a / z;
}

int all_checkers(int a) {
  int z = 0;
  // codechecker_false_positive [all] matches every checker
  return a / z;
}

int substring_match(int a) {
  int z = 0;
  // codechecker_intentional [Divide] a part of the checker name is enough
  return a / z;
}

int other_checker(int a) {
  int z = 0;
  // codechecker_false_positive [core.NullDereference] names another checker
  return a / z;
}

int checker_list(int a) {
  int z = 0;
  // codechecker_confirmed [core.NullDereference, core.DivideZero] two checkers
  return a / z;
}

int multi_line(int a) {
  int z = 0;
  // codechecker_intentional [core.DivideZero] this reason
  // continues on a second line
  return a / z;
}

int c_style(int a) {
  int z = 0;
  /* codechecker_false_positive [core.DivideZero]
   * reason in a C-style comment */
  return a / z;
}

int missing_reason(int a) {
  int z = 0;
  // codechecker_confirmed [core.DivideZero]
  return a / z;
}

int misspelled_marker(int a) {
  int z = 0;
  // codechecker_suppressss [all] contains a marker name, so parse warns
  return a / z;
}

int not_a_marker(int a) {
  int z = 0;
  // codechecker_supress [all] contains no marker name: an ordinary comment
  return a / z;
}

int not_adjacent(int a) {
  // codechecker_false_positive [all] code between the comment and the bug
  int z = 0;
  return a / z;
}
