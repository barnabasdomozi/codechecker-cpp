// Two comments that both match the same report. 'parse' reports an error.

int two_markers(int a) {
  int z = 0;
  // codechecker_false_positive [core.DivideZero] first comment
  // codechecker_confirmed [core.DivideZero] second comment
  return a / z;
}
