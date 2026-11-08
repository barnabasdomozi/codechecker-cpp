// A pleasant color for a name, such as a product endpoint: the same name
// always gets the same hue, with a fixed saturation and lightness.

export function nameToHue(name) {
  let hash = 0;
  for (const char of name || "?") {
    hash = (hash * 31 + char.charCodeAt(0)) | 0;
  }
  return Math.abs(hash) % 360;
}

export function accentColor(name, alpha = 1) {
  return `hsl(${nameToHue(name)} 72% 56% / ${alpha})`;
}

// Two close shades of the name's hue, for gradients.
export function accentGradient(name) {
  const hue = nameToHue(name);
  return `linear-gradient(135deg, hsl(${hue} 78% 58%), ` +
    `hsl(${(hue + 12) % 360} 74% 50%))`;
}
