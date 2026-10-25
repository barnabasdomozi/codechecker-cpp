// Jest configuration for the black-box suite. See README.md for how to run it.
/** @type {import('jest').Config} */
module.exports = {
  testEnvironment: 'node',
  roots: ['<rootDir>/tests'],
  testMatch: ['**/*.test.ts'],
  moduleFileExtensions: ['ts', 'js', 'json'],
  globalSetup: '<rootDir>/src/global-setup.ts',
  globalTeardown: '<rootDir>/src/global-teardown.ts',
  setupFilesAfterEnv: ['<rootDir>/src/matchers.ts'],
  // Analyses are slow; a single test may run several CodeChecker commands.
  testTimeout: 300_000,
  // Each worker runs analyzers that use several processes themselves.
  maxWorkers: '50%',
  verbose: false,
};
