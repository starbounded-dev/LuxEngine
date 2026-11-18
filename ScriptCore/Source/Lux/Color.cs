// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using System.Globalization;

namespace Lux
{
	/// <summary>
	/// A linear RGBA color with components from 0 to 1. Converts to and from <see cref="Vector4"/>, so a color can be
	/// assigned to any component property that takes one (<c>sprite.Color = Color.Red</c>).
	/// </summary>
	public struct Color : IEquatable<Color>
	{
		/// <summary>The red component.</summary>
		public float R;
		/// <summary>The green component.</summary>
		public float G;
		/// <summary>The blue component.</summary>
		public float B;
		/// <summary>The alpha component: 0 is transparent, 1 opaque.</summary>
		public float A;

		/// <summary>An opaque color.</summary>
		public Color(float r, float g, float b)
		{
			R = r;
			G = g;
			B = b;
			A = 1.0f;
		}

		/// <summary>A color from its four components.</summary>
		public Color(float r, float g, float b, float a)
		{
			R = r;
			G = g;
			B = b;
			A = a;
		}

		/// <summary>Opaque red.</summary>
		public static Color Red => new Color(1.0f, 0.0f, 0.0f);
		/// <summary>Opaque green.</summary>
		public static Color Green => new Color(0.0f, 1.0f, 0.0f);
		/// <summary>Opaque blue.</summary>
		public static Color Blue => new Color(0.0f, 0.0f, 1.0f);
		/// <summary>Opaque yellow.</summary>
		public static Color Yellow => new Color(1.0f, 1.0f, 0.0f);
		/// <summary>Opaque cyan.</summary>
		public static Color Cyan => new Color(0.0f, 1.0f, 1.0f);
		/// <summary>Opaque magenta.</summary>
		public static Color Magenta => new Color(1.0f, 0.0f, 1.0f);
		/// <summary>Opaque white.</summary>
		public static Color White => new Color(1.0f, 1.0f, 1.0f);
		/// <summary>Opaque black.</summary>
		public static Color Black => new Color(0.0f, 0.0f, 0.0f);
		/// <summary>Opaque mid gray.</summary>
		public static Color Gray => new Color(0.5f, 0.5f, 0.5f);
		/// <summary>Fully transparent black.</summary>
		public static Color Clear => new Color(0.0f, 0.0f, 0.0f, 0.0f);

		/// <summary>The color <paramref name="t"/> of the way from <paramref name="a"/> to <paramref name="b"/>, with <paramref name="t"/> limited to 0..1.</summary>
		public static Color Lerp(Color a, Color b, float t)
		{
			t = Mathf.Clamp01(t);
			return new Color(a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t, a.A + (b.A - a.A) * t);
		}

		/// <summary>A color from hue, saturation and value, each 0..1 (hue wraps).</summary>
		public static Color FromHSV(float hue, float saturation, float value, float alpha = 1.0f)
		{
			hue = Mathf.Repeat(hue, 1.0f) * 6.0f;
			float c = value * saturation;
			float x = c * (1.0f - Math.Abs(Mathf.Repeat(hue, 2.0f) - 1.0f));
			float m = value - c;
			float r, g, b;
			if (hue < 1.0f) { r = c; g = x; b = 0.0f; }
			else if (hue < 2.0f) { r = x; g = c; b = 0.0f; }
			else if (hue < 3.0f) { r = 0.0f; g = c; b = x; }
			else if (hue < 4.0f) { r = 0.0f; g = x; b = c; }
			else if (hue < 5.0f) { r = x; g = 0.0f; b = c; }
			else { r = c; g = 0.0f; b = x; }
			return new Color(r + m, g + m, b + m, alpha);
		}

		/// <summary>The color as hue, saturation and value, each 0..1.</summary>
		public void ToHSV(out float hue, out float saturation, out float value)
		{
			float max = Math.Max(R, Math.Max(G, B)), min = Math.Min(R, Math.Min(G, B));
			float delta = max - min;
			value = max;
			saturation = max > 0.0f ? delta / max : 0.0f;
			if (delta <= 0.0f)
				hue = 0.0f;
			else if (max == R)
				hue = Mathf.Repeat((G - B) / delta, 6.0f) / 6.0f;
			else if (max == G)
				hue = ((B - R) / delta + 2.0f) / 6.0f;
			else
				hue = ((R - G) / delta + 4.0f) / 6.0f;
		}

		/// <summary>Parses "RRGGBB" or "RRGGBBAA" (a leading # is allowed), for example "#ff8800".</summary>
		/// <returns>The color, or false in <paramref name="success"/> when the text is not a color.</returns>
		public static Color FromHex(string hex, out bool success)
		{
			success = false;
			if (hex == null)
				return White;
			hex = hex.TrimStart('#');
			if ((hex.Length != 6 && hex.Length != 8) || !uint.TryParse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint packed))
				return White;
			success = true;
			if (hex.Length == 6)
				packed = (packed << 8) | 0xFFu;
			return new Color(((packed >> 24) & 0xFF) / 255.0f, ((packed >> 16) & 0xFF) / 255.0f, ((packed >> 8) & 0xFF) / 255.0f, (packed & 0xFF) / 255.0f);
		}

		/// <summary>The color as "RRGGBBAA" hex digits.</summary>
		public string ToHex()
		{
			static int Byte(float v) => (int)Math.Round(Mathf.Clamp01(v) * 255.0f);
			return $"{Byte(R):X2}{Byte(G):X2}{Byte(B):X2}{Byte(A):X2}";
		}

		/// <summary>The color with its alpha replaced.</summary>
		public Color WithAlpha(float alpha) => new Color(R, G, B, alpha);

		/// <summary>Component-wise sum.</summary>
		public static Color operator +(Color a, Color b) => new Color(a.R + b.R, a.G + b.G, a.B + b.B, a.A + b.A);
		/// <summary>Component-wise difference.</summary>
		public static Color operator -(Color a, Color b) => new Color(a.R - b.R, a.G - b.G, a.B - b.B, a.A - b.A);
		/// <summary>Component-wise product, for tinting one color by another.</summary>
		public static Color operator *(Color a, Color b) => new Color(a.R * b.R, a.G * b.G, a.B * b.B, a.A * b.A);
		/// <summary>Every component scaled by <paramref name="scalar"/>.</summary>
		public static Color operator *(Color color, float scalar) => new Color(color.R * scalar, color.G * scalar, color.B * scalar, color.A * scalar);
		/// <summary>Whether the components are exactly equal.</summary>
		public static bool operator ==(Color a, Color b) => a.R == b.R && a.G == b.G && a.B == b.B && a.A == b.A;
		/// <summary>Whether any component differs.</summary>
		public static bool operator !=(Color a, Color b) => !(a == b);

		/// <summary>Reads a <see cref="Vector4"/> as red, green, blue, alpha.</summary>
		public static implicit operator Color(Vector4 v) => new Color(v.X, v.Y, v.Z, v.W);
		/// <summary>Writes the color as a <see cref="Vector4"/> of red, green, blue, alpha.</summary>
		public static implicit operator Vector4(Color c) => new Vector4(c.R, c.G, c.B, c.A);

		/// <summary>Whether the components are exactly equal.</summary>
		public bool Equals(Color other) => this == other;
		public override bool Equals(object? obj) => obj is Color other && this == other;
		public override int GetHashCode() => HashCode.Combine(R, G, B, A);
		public override string ToString() => $"RGBA({R:F3}, {G:F3}, {B:F3}, {A:F3})";
	}
}
