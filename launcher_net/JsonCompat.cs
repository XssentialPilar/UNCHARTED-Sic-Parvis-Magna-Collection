#if NETFRAMEWORK
using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text;

namespace System.Runtime.CompilerServices
{
    internal static class IsExternalInit { }
}

namespace System.Text.Json.Serialization
{
    [AttributeUsage(AttributeTargets.Property | AttributeTargets.Field)]
    internal sealed class JsonPropertyNameAttribute : Attribute
    {
        public string Name { get; }
        public JsonPropertyNameAttribute(string name) => Name = name;
    }

    [AttributeUsage(AttributeTargets.Property | AttributeTargets.Field)]
    internal sealed class JsonIgnoreAttribute : Attribute
    {
    }
}

namespace UnchartedLauncher
{
    internal static class StringCompatExtensions
    {
        public static bool Contains(this string source, string value, StringComparison comparisonType)
        {
            if (source == null) return false;
            if (value == null) return false;
            return source.IndexOf(value, comparisonType) >= 0;
        }
    }

    internal static class JsonCompat
    {
        private static string GetJsonName(PropertyInfo prop)
        {
            var attr = prop.GetCustomAttribute<System.Text.Json.Serialization.JsonPropertyNameAttribute>();
            return attr != null ? attr.Name : prop.Name;
        }

        private static bool ShouldIgnore(PropertyInfo prop)
        {
            return prop.GetCustomAttribute<System.Text.Json.Serialization.JsonIgnoreAttribute>() != null;
        }

        public static LauncherConfig? DeserializeConfig(string json)
        {
            if (string.IsNullOrWhiteSpace(json)) return null;
            if (MiniJson.Parse(json) is not Dictionary<string, object?> root) return null;

            var ciRoot = new Dictionary<string, object?>(StringComparer.OrdinalIgnoreCase);
            foreach (var kv in root)
                ciRoot[kv.Key] = kv.Value;

            var cfg = new LauncherConfig();
            foreach (var prop in typeof(LauncherConfig).GetProperties(BindingFlags.Public | BindingFlags.Instance))
            {
                if (!prop.CanWrite || ShouldIgnore(prop)) continue;
                string key = GetJsonName(prop);
                if (!ciRoot.TryGetValue(key, out object? raw) && !ciRoot.TryGetValue(prop.Name, out raw))
                    continue;
                if (raw == null) continue;

                try
                {
                    Type t = Nullable.GetUnderlyingType(prop.PropertyType) ?? prop.PropertyType;
                    if (t == typeof(string))
                    {
                        prop.SetValue(cfg, Convert.ToString(raw, CultureInfo.InvariantCulture) ?? "");
                    }
                    else if (t == typeof(bool))
                    {
                        if (raw is bool b) prop.SetValue(cfg, b);
                        else if (bool.TryParse(Convert.ToString(raw, CultureInfo.InvariantCulture), out bool pb)) prop.SetValue(cfg, pb);
                    }
                    else if (t == typeof(double))
                    {
                        prop.SetValue(cfg, Convert.ToDouble(raw, CultureInfo.InvariantCulture));
                    }
                    else if (t == typeof(float))
                    {
                        prop.SetValue(cfg, Convert.ToSingle(raw, CultureInfo.InvariantCulture));
                    }
                    else if (t == typeof(int))
                    {
                        prop.SetValue(cfg, Convert.ToInt32(raw, CultureInfo.InvariantCulture));
                    }
                    else if (t == typeof(long))
                    {
                        prop.SetValue(cfg, Convert.ToInt64(raw, CultureInfo.InvariantCulture));
                    }
                }
                catch
                {
                    // Ignore malformed fields so partial configs load cleanly
                }
            }
            return cfg;
        }

        public static string SerializeConfig(LauncherConfig cfg)
        {
            var sb = new StringBuilder();
            sb.AppendLine("{");
            var props = typeof(LauncherConfig).GetProperties(BindingFlags.Public | BindingFlags.Instance);
            var entries = new List<string>();
            foreach (var prop in props)
            {
                if (!prop.CanRead || !prop.CanWrite || ShouldIgnore(prop)) continue;
                string key = GetJsonName(prop);
                object? val = prop.GetValue(cfg);
                entries.Add($"  {EscapeString(key)}: {FormatValue(val)}");
            }
            for (int i = 0; i < entries.Count; i++)
            {
                sb.Append(entries[i]);
                if (i + 1 < entries.Count) sb.Append(',');
                sb.AppendLine();
            }
            sb.Append('}');
            return sb.ToString();
        }

        private static string FormatValue(object? val)
        {
            if (val == null) return "null";
            if (val is bool b) return b ? "true" : "false";
            if (val is int i) return i.ToString(CultureInfo.InvariantCulture);
            if (val is long l) return l.ToString(CultureInfo.InvariantCulture);
            if (val is double d) return double.IsNaN(d) || double.IsInfinity(d) ? "0" : d.ToString("G17", CultureInfo.InvariantCulture);
            if (val is float f) return float.IsNaN(f) || float.IsInfinity(f) ? "0" : f.ToString("G9", CultureInfo.InvariantCulture);
            if (val is string s) return EscapeString(s);
            if (val is IDictionary dict)
            {
                var sb = new StringBuilder();
                sb.Append('{');
                bool first = true;
                foreach (DictionaryEntry e in dict)
                {
                    if (!first) sb.Append(", ");
                    first = false;
                    sb.Append(EscapeString(Convert.ToString(e.Key, CultureInfo.InvariantCulture) ?? ""));
                    sb.Append(": ");
                    sb.Append(FormatValue(e.Value));
                }
                sb.Append('}');
                return sb.ToString();
            }
            return EscapeString(Convert.ToString(val, CultureInfo.InvariantCulture) ?? "");
        }

        private static string EscapeString(string s)
        {
            var sb = new StringBuilder(s.Length + 2);
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\b': sb.Append("\\b"); break;
                    case '\f': sb.Append("\\f"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < ' ')
                            sb.AppendFormat(CultureInfo.InvariantCulture, "\\u{0:x4}", (int)c);
                        else
                            sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
            return sb.ToString();
        }

        public static Dictionary<string, Dictionary<string, string>>? DeserializeTranslations(string json)
        {
            if (string.IsNullOrWhiteSpace(json)) return null;
            if (MiniJson.Parse(json) is not Dictionary<string, object?> root) return null;

            var result = new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);
            foreach (var langEntry in root)
            {
                if (langEntry.Value is not Dictionary<string, object?> mapObj) continue;
                var inner = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (var kv in mapObj)
                {
                    if (kv.Value != null)
                        inner[kv.Key] = Convert.ToString(kv.Value, CultureInfo.InvariantCulture) ?? "";
                }
                result[langEntry.Key] = inner;
            }
            return result;
        }

        public sealed class ReleaseAssetInfo
        {
            public string Name { get; set; } = "";
            public string BrowserDownloadUrl { get; set; } = "";
            public long Size { get; set; }
        }

        public static bool TryParseLatestRelease(
            string json,
            out string latestTag,
            out string htmlUrl,
            out List<ReleaseAssetInfo> assets)
        {
            latestTag = "";
            htmlUrl = "";
            assets = new List<ReleaseAssetInfo>();
            if (string.IsNullOrWhiteSpace(json)) return false;

            try
            {
                var parsed = MiniJson.Parse(json);
                IDictionary<string, object?>? rel = null;
                if (parsed is Dictionary<string, object?> dict)
                    rel = dict;
                else if (parsed is List<object?> list && list.Count > 0)
                    rel = list[0] as Dictionary<string, object?>;
                if (rel == null) return false;

                if (rel.TryGetValue("tag_name", out var tagObj) && tagObj != null)
                    latestTag = Convert.ToString(tagObj, CultureInfo.InvariantCulture)?.Trim().TrimStart('v', 'V') ?? "";
                if (rel.TryGetValue("html_url", out var urlObj) && urlObj != null)
                    htmlUrl = Convert.ToString(urlObj, CultureInfo.InvariantCulture) ?? "";
                if (rel.TryGetValue("assets", out var assetsObj) && assetsObj is List<object?> assetsList)
                {
                    foreach (var item in assetsList)
                    {
                        if (item is not Dictionary<string, object?> a) continue;
                        var info = new ReleaseAssetInfo();
                        if (a.TryGetValue("name", out var n) && n != null)
                            info.Name = Convert.ToString(n, CultureInfo.InvariantCulture) ?? "";
                        if (a.TryGetValue("browser_download_url", out var u) && u != null)
                            info.BrowserDownloadUrl = Convert.ToString(u, CultureInfo.InvariantCulture) ?? "";
                        if (a.TryGetValue("size", out var sz) && sz != null)
                        {
                            try { info.Size = Convert.ToInt64(sz, CultureInfo.InvariantCulture); }
                            catch { info.Size = 0; }
                        }
                        assets.Add(info);
                    }
                }
                return !string.IsNullOrEmpty(latestTag);
            }
            catch
            {
                return false;
            }
        }

        private sealed class MiniJson
        {
            private readonly string _src;
            private int _pos;

            private MiniJson(string src)
            {
                _src = src;
                _pos = 0;
            }

            public static object? Parse(string json)
            {
                if (string.IsNullOrEmpty(json)) return null;
                var p = new MiniJson(json);
                p.SkipWhitespace();
                return p.ParseValue();
            }

            private void SkipWhitespace()
            {
                while (_pos < _src.Length && char.IsWhiteSpace(_src[_pos]))
                    _pos++;
            }

            private object? ParseValue()
            {
                SkipWhitespace();
                if (_pos >= _src.Length) return null;
                char c = _src[_pos];
                return c switch
                {
                    '{' => ParseObject(),
                    '[' => ParseArray(),
                    '"' => ParseString(),
                    't' or 'f' => ParseBool(),
                    'n' => ParseNull(),
                    _ => ParseNumber()
                };
            }

            private Dictionary<string, object?> ParseObject()
            {
                var obj = new Dictionary<string, object?>(StringComparer.Ordinal);
                _pos++; // '{'
                while (true)
                {
                    SkipWhitespace();
                    if (_pos >= _src.Length) break;
                    if (_src[_pos] == '}') { _pos++; break; }
                    if (_src[_pos] == ',') { _pos++; continue; }

                    string key = ParseString();
                    SkipWhitespace();
                    if (_pos < _src.Length && _src[_pos] == ':') _pos++;
                    object? val = ParseValue();
                    obj[key] = val;
                }
                return obj;
            }

            private List<object?> ParseArray()
            {
                var arr = new List<object?>();
                _pos++; // '['
                while (true)
                {
                    SkipWhitespace();
                    if (_pos >= _src.Length) break;
                    if (_src[_pos] == ']') { _pos++; break; }
                    if (_src[_pos] == ',') { _pos++; continue; }
                    arr.Add(ParseValue());
                }
                return arr;
            }

            private string ParseString()
            {
                if (_pos >= _src.Length || _src[_pos] != '"') return "";
                _pos++;
                var sb = new StringBuilder();
                while (_pos < _src.Length)
                {
                    char c = _src[_pos++];
                    if (c == '"') break;
                    if (c == '\\' && _pos < _src.Length)
                    {
                        char esc = _src[_pos++];
                        switch (esc)
                        {
                            case '"': sb.Append('"'); break;
                            case '\\': sb.Append('\\'); break;
                            case '/': sb.Append('/'); break;
                            case 'b': sb.Append('\b'); break;
                            case 'f': sb.Append('\f'); break;
                            case 'n': sb.Append('\n'); break;
                            case 'r': sb.Append('\r'); break;
                            case 't': sb.Append('\t'); break;
                            case 'u':
                                if (_pos + 4 <= _src.Length &&
                                    int.TryParse(_src.Substring(_pos, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out int cp))
                                {
                                    sb.Append((char)cp);
                                    _pos += 4;
                                }
                                break;
                            default:
                                sb.Append(esc);
                                break;
                        }
                    }
                    else
                    {
                        sb.Append(c);
                    }
                }
                return sb.ToString();
            }

            private object? ParseBool()
            {
                if (_pos + 4 <= _src.Length && string.Compare(_src, _pos, "true", 0, 4, StringComparison.Ordinal) == 0)
                {
                    _pos += 4;
                    return true;
                }
                if (_pos + 5 <= _src.Length && string.Compare(_src, _pos, "false", 0, 5, StringComparison.Ordinal) == 0)
                {
                    _pos += 5;
                    return false;
                }
                _pos++;
                return null;
            }

            private object? ParseNull()
            {
                if (_pos + 4 <= _src.Length && string.Compare(_src, _pos, "null", 0, 4, StringComparison.Ordinal) == 0)
                    _pos += 4;
                else
                    _pos++;
                return null;
            }

            private object? ParseNumber()
            {
                int start = _pos;
                bool isFloat = false;
                while (_pos < _src.Length)
                {
                    char c = _src[_pos];
                    if ((c >= '0' && c <= '9') || c == '-' || c == '+')
                    {
                        _pos++;
                    }
                    else if (c == '.' || c == 'e' || c == 'E')
                    {
                        isFloat = true;
                        _pos++;
                    }
                    else
                    {
                        break;
                    }
                }
                if (_pos == start) { _pos++; return 0; }
                string slice = _src.Substring(start, _pos - start);
                if (!isFloat && long.TryParse(slice, NumberStyles.Integer, CultureInfo.InvariantCulture, out long lv))
                    return lv;
                if (double.TryParse(slice, NumberStyles.Float, CultureInfo.InvariantCulture, out double dv))
                    return dv;
                return 0;
            }
        }
    }
}
#endif
