// Bugs without source-code comments, for the rules in review_status.yaml.

int by_hash(int a) {
  int z = 0;
  return a / z;
}

int by_path_and_checker(int a) {
  int z = 0;
  return a / z;
}

int by_checker() {
  int *p = nullptr;
  return *p;
}

int comment_wins(int a) {
  int z = 0;
  // codechecker_intentional [core.DivideZero] a source comment beats a rule
  return a / z;
}
