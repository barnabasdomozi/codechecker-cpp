// Jest runs the TypeScript sources through Babel, which only removes the
// types. Type checking is done separately with 'npm run typecheck'.
module.exports = {
  presets: [['@babel/preset-typescript', { onlyRemoveTypeImports: true }]],
  plugins: ['@babel/plugin-transform-modules-commonjs'],
};
