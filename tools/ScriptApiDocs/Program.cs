// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
//
// Writes the manual's C# API reference from what the scripting assembly really contains:
//
//   dotnet run --project tools/ScriptApiDocs -- <ScriptCore.dll> <docs dir> [--check]
//
// ScriptCore.xml (the compiler's documentation file) must sit next to the dll. Every public type in
// namespace Lux gets a page under <docs dir>/api, plus api/index.md. Members come from reflection over the
// built assembly, text from the XML comments, so a page cannot describe a member that does not exist.
// --check writes nothing and fails (exit 1) when a page is out of date, a public member has no <summary>,
// or a documentation comment names a member the assembly lacks.

using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Xml.Linq;

if (args.Length < 2)
{
	Console.Error.WriteLine("usage: ScriptApiDocs <ScriptCore.dll> <docs dir> [--check]");
	return 2;
}

string assemblyPath = Path.GetFullPath(args[0]);
string docsDir = Path.GetFullPath(args[1]);
bool check = args.Contains("--check");
string xmlPath = Path.ChangeExtension(assemblyPath, ".xml");
if (!File.Exists(xmlPath))
{
	Console.Error.WriteLine($"No documentation file at {xmlPath} (GenerateDocumentationFile in ScriptCore's premake).");
	return 2;
}

var context = new AssemblyLoadContext("ScriptApiDocs", isCollectible: false);
string assemblyDir = Path.GetDirectoryName(assemblyPath)!;
context.Resolving += (ctx, name) =>
{
	string candidate = Path.Combine(assemblyDir, name.Name + ".dll");
	return File.Exists(candidate) ? ctx.LoadFromAssemblyPath(candidate) : null;
};
Assembly assembly = context.LoadFromAssemblyPath(assemblyPath);

var docs = XDocument.Load(xmlPath).Root!.Element("members")!.Elements("member")
	.ToDictionary(m => (string)m.Attribute("name")!, m => m);

var problems = new List<string>();
var documentedIds = new HashSet<string>();

Type[] types = assembly.GetExportedTypes()
	.Where(t => t.Namespace == "Lux" && !t.IsNested && t.GetCustomAttribute<System.Runtime.CompilerServices.CompilerGeneratedAttribute>() == null)
	.OrderBy(t => t.Name, StringComparer.Ordinal).ToArray();

var pages = new SortedDictionary<string, string>(StringComparer.Ordinal);
foreach (Type type in types)
	pages[$"api/{type.Name}.md"] = TypePage(type);
pages["api/index.md"] = IndexPage(types);

// Comments for members that no longer exist: the compiler keeps them only if it can bind them, so this
// mostly catches cref-free leftovers on members that became non-public.
foreach (string id in docs.Keys)
{
	if (id.StartsWith("T:Lux.") || id.StartsWith("M:Lux.") || id.StartsWith("P:Lux.") || id.StartsWith("F:Lux.") || id.StartsWith("E:Lux."))
	{
		if (!documentedIds.Contains(id) && !id.Contains("InternalCalls") && !IsNonPublicId(id))
			problems.Add($"documented but not public in the assembly: {id}");
	}
}

int stale = 0;
string apiDir = Path.Combine(docsDir, "api");
var expected = new HashSet<string>(pages.Keys.Select(p => Path.GetFullPath(Path.Combine(docsDir, p))), StringComparer.OrdinalIgnoreCase);
foreach (var (relative, text) in pages)
{
	string path = Path.Combine(docsDir, relative);
	// git may check the pages out with CRLF (core.autocrlf), so line endings don't count.
	string? current = File.Exists(path) ? File.ReadAllText(path).Replace("\r\n", "\n") : null;
	if (current == text)
		continue;
	stale++;
	if (check)
		problems.Add($"out of date: {relative}");
	else
	{
		Directory.CreateDirectory(Path.GetDirectoryName(path)!);
		File.WriteAllText(path, text, new UTF8Encoding(false));
	}
}
if (Directory.Exists(apiDir))
{
	foreach (string file in Directory.GetFiles(apiDir, "*.md"))
	{
		if (expected.Contains(Path.GetFullPath(file)))
			continue;
		stale++;
		if (check)
			problems.Add($"no longer in the assembly: api/{Path.GetFileName(file)}");
		else
			File.Delete(file);
	}
}

foreach (string problem in problems)
	Console.WriteLine(problem);
Console.WriteLine(check
	? $"{pages.Count} pages checked, {stale} out of date, {problems.Count} problem(s)."
	: $"{pages.Count} pages, {stale} written or removed, {problems.Count} problem(s).");
return check && problems.Count > 0 ? 1 : 0;

// ---- pages -----------------------------------------------------------------------------------------

string IndexPage(Type[] all)
{
	var text = new StringBuilder();
	text.Append("# C# API Reference\n\n");
	text.Append("Everything a script can use from the `Lux` namespace, generated from the scripting assembly the editor ships with. ");
	text.Append("For how scripts fit together, start with [C# Scripts](../scripting/csharp-scripts).\n\n");
	foreach (var group in all.GroupBy(Category).OrderBy(g => CategoryOrder(g.Key)))
	{
		text.Append($"## {group.Key}\n\n");
		foreach (Type type in group)
			text.Append($"- [{type.Name}]({type.Name}) — {OneLine(Summary(type))}\n");
		text.Append('\n');
	}
	return text.ToString();
}

string TypePage(Type type)
{
	var text = new StringBuilder();
	text.Append($"# {type.Name}\n\n");
	text.Append($"`{Declaration(type)}`\n\n");
	AppendBody(text, Summary(type), Remarks(type));
	AppendMembers(text, type, "##");
	foreach (Type nested in type.GetNestedTypes(BindingFlags.Public).OrderBy(t => t.Name, StringComparer.Ordinal))
	{
		text.Append($"## {nested.Name}\n\n`{Declaration(nested)}`\n\n");
		AppendBody(text, Summary(nested), Remarks(nested));
		AppendMembers(text, nested, "###");
	}
	text.Append("[All types](index)\n");
	return text.ToString();
}

void AppendBody(StringBuilder text, string summary, string remarks)
{
	if (summary.Length > 0)
		text.Append(summary).Append("\n\n");
	if (remarks.Length > 0)
		text.Append(remarks).Append("\n\n");
}

void AppendMembers(StringBuilder text, Type type, string heading)
{
	const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;

	if (type.IsEnum)
	{
		text.Append($"{heading} Values\n\n| Name | Value | Description |\n|---|---|---|\n");
		foreach (FieldInfo field in type.GetFields(BindingFlags.Public | BindingFlags.Static).OrderBy(f => Convert.ToInt64(f.GetRawConstantValue())))
			text.Append($"| `{field.Name}` | {Convert.ToInt64(field.GetRawConstantValue())} | {Cell(MemberSummary(field, "F", required: false))} |\n");
		text.Append('\n');
		return;
	}

	var fields = type.GetFields(flags).Where(f => f.IsPublic && !f.IsSpecialName).OrderBy(f => f.Name, StringComparer.Ordinal).ToArray();
	var constructors = type.GetConstructors(BindingFlags.Public | BindingFlags.Instance).Where(c => c.GetParameters().Length > 0).ToArray(); // a default constructor is usually compiler-generated and cannot carry a comment
	var properties = type.GetProperties(flags).Where(p => (p.GetMethod?.IsPublic ?? false) || (p.SetMethod?.IsPublic ?? false)).OrderBy(p => p.Name, StringComparer.Ordinal).ToArray();
	var events = type.GetEvents(flags).Where(e => e.AddMethod?.IsPublic ?? false).OrderBy(e => e.Name, StringComparer.Ordinal).ToArray();
	var methods = type.GetMethods(flags)
		.Where(m => !m.IsSpecialName && (m.IsPublic || m.IsFamily) && m.GetBaseDefinition().DeclaringType == m.DeclaringType)
		.OrderBy(m => m.IsFamily).ThenBy(m => m.Name, StringComparer.Ordinal).ThenBy(m => m.GetParameters().Length).ToArray();
	var operators = type.GetMethods(flags).Where(m => m.IsSpecialName && m.IsPublic && m.Name.StartsWith("op_")).ToArray();

	if (fields.Length > 0)
	{
		text.Append($"{heading} Fields\n\n| Name | Type | Description |\n|---|---|---|\n");
		foreach (FieldInfo field in fields)
			text.Append($"| `{field.Name}` | `{TypeName(field.FieldType)}` | {Cell(MemberSummary(field, "F"))} |\n");
		text.Append('\n');
	}
	if (properties.Length > 0)
	{
		text.Append($"{heading} Properties\n\n| Name | Type | Access | Description |\n|---|---|---|---|\n");
		foreach (PropertyInfo property in properties)
		{
			bool get = property.GetMethod?.IsPublic ?? false, set = property.SetMethod?.IsPublic ?? false;
			string access = get && set ? "get, set" : get ? "get" : "set";
			string type2 = TypeName(property.PropertyType) + (IsNullable(property) ? "?" : "");
			text.Append($"| `{property.Name}` | `{type2}` | {access} | {Cell(MemberSummary(property, "P"))} |\n");
		}
		text.Append('\n');
	}
	if (events.Length > 0)
	{
		text.Append($"{heading} Events\n\n| Name | Type | Description |\n|---|---|---|\n");
		foreach (EventInfo e in events)
			text.Append($"| `{e.Name}` | `{TypeName(e.EventHandlerType!)}` | {Cell(MemberSummary(e, "E"))} |\n");
		text.Append('\n');
	}
	var callables = constructors.Cast<MethodBase>().Concat(methods).Concat(operators).ToArray();
	if (callables.Length > 0)
	{
		text.Append($"{heading} Methods\n\n");
		foreach (MethodBase method in callables)
		{
			text.Append($"```\n{Signature(method)}\n```\n\n");
			string summary = MemberSummary(method, "M");
			if (summary.Length > 0)
				text.Append(summary).Append("\n\n");
			string parameters = ParamDocs(method);
			if (parameters.Length > 0)
				text.Append(parameters).Append('\n');
			string returns = Returns(method);
			if (returns.Length > 0)
				text.Append($"Returns: {returns}\n\n");
		}
	}
}

// ---- documentation lookup ----------------------------------------------------------------------------

string Summary(Type type) => Text(Doc("T:" + IdName(type), type.FullName!, isType: true), "summary");
string Remarks(Type type) => Text(docs.GetValueOrDefault("T:" + IdName(type)), "remarks");

// Enum values are optional: most (KeyCode.A) say nothing a description could add.
string MemberSummary(MemberInfo member, string prefix, bool required = true)
{
	string id = MemberId(member, prefix);
	return Text(Doc(id, $"{member.DeclaringType!.Name}.{member.Name}", isType: false, required), "summary");
}

string ParamDocs(MethodBase method)
{
	XElement? doc = docs.GetValueOrDefault(MemberId(method, "M"));
	if (doc == null)
		return "";
	var text = new StringBuilder();
	foreach (XElement param in doc.Elements("param"))
	{
		string body = Normalize(param);
		if (body.Length > 0)
			text.Append($"- `{(string)param.Attribute("name")!}`: {body}\n");
	}
	return text.ToString();
}

string Returns(MethodBase method)
{
	XElement? doc = docs.GetValueOrDefault(MemberId(method, "M"));
	return doc?.Element("returns") is XElement returns ? Normalize(returns) : "";
}

XElement? Doc(string id, string readable, bool isType, bool required = true)
{
	documentedIds.Add(id);
	if (docs.TryGetValue(id, out XElement? doc) && Normalize(doc.Element("summary")).Length > 0)
		return doc;
	if (required && !problems.Contains($"no <summary>: {readable}"))
		problems.Add($"no <summary>: {readable}");
	return doc;
}

string Text(XElement? doc, string element) => doc?.Element(element) is XElement e ? Normalize(e) : "";

// The XML comment's text with <c>, <see cref>, <paramref> and <para> turned into Markdown.
string Normalize(XElement? element)
{
	if (element == null)
		return "";
	var text = new StringBuilder();
	foreach (XNode node in element.Nodes())
	{
		switch (node)
		{
			case XText t:
				text.Append(t.Value);
				break;
			case XElement e when e.Name == "c":
				text.Append('`').Append(e.Value).Append('`');
				break;
			case XElement e when e.Name == "see" || e.Name == "seealso":
				string target = (string?)e.Attribute("cref") ?? (string?)e.Attribute("langword") ?? "";
				string name = target.Contains(':') ? target[(target.IndexOf(':') + 1)..] : target;
				name = name.StartsWith("Lux.") ? name[4..] : name;
				int paren = name.IndexOf('(');
				if (paren >= 0)
					name = name[..paren];
				string page = name.Split('.')[0];
				text.Append(target.StartsWith("T:Lux.") || (target.Length > 2 && target[1] == ':' && target[2..].StartsWith("Lux."))
					? $"[`{name}`]({page})" : $"`{name}`");
				break;
			case XElement e when e.Name == "paramref" || e.Name == "typeparamref":
				text.Append('`').Append((string?)e.Attribute("name")).Append('`');
				break;
			case XElement e when e.Name == "para":
				text.Append("\n\n").Append(Normalize(e)).Append("\n\n");
				break;
			case XElement e:
				text.Append(Normalize(e));
				break;
		}
	}
	// Collapse the comment's line breaks and indentation, keeping paragraph breaks.
	var paragraphs = text.ToString().Replace("\r", "").Split("\n\n", StringSplitOptions.RemoveEmptyEntries)
		.Select(p => string.Join(' ', p.Split('\n', StringSplitOptions.RemoveEmptyEntries).Select(l => l.Trim())).Trim())
		.Where(p => p.Length > 0);
	return string.Join("\n\n", paragraphs);
}

string Cell(string text) => text.Replace("\n\n", "<br>").Replace("|", "\\|");
string OneLine(string text) { int end = text.IndexOf(". "); return end > 0 ? text[..(end + 1)] : text; }

bool IsNonPublicId(string id)
{
	// Private helpers sometimes carry comments; only public API is required to match.
	return false;
}

// ---- names and documentation IDs ---------------------------------------------------------------------

string IdName(Type type) => type.IsNested ? IdName(type.DeclaringType!) + "." + type.Name : type.FullName!;

string MemberId(MemberInfo member, string prefix)
{
	string owner = IdName(member.DeclaringType!);
	if (member is MethodBase method)
	{
		string name = method is ConstructorInfo ? "#ctor" : method.Name;
		if (method.IsGenericMethodDefinition)
			name += "``" + method.GetGenericArguments().Length;
		var parameters = method.GetParameters();
		string args = parameters.Length == 0 ? "" : "(" + string.Join(",", parameters.Select(p => IdType(p.ParameterType))) + ")";
		string conversion = method.Name is "op_Implicit" or "op_Explicit" ? "~" + IdType(((MethodInfo)method).ReturnType) : "";
		return $"{prefix}:{owner}.{name}{args}{conversion}";
	}
	return $"{prefix}:{owner}.{member.Name}";
}

string IdType(Type type)
{
	if (type.IsByRef)
		return IdType(type.GetElementType()!) + "@";
	if (type.IsPointer)
		return IdType(type.GetElementType()!) + "*";
	if (type.IsArray)
		return IdType(type.GetElementType()!) + "[]";
	if (type.IsGenericType)
	{
		string name = type.GetGenericTypeDefinition().FullName!;
		name = name[..name.IndexOf('`')];
		return name + "{" + string.Join(",", type.GetGenericArguments().Select(IdType)) + "}";
	}
	if (type.IsGenericParameter)
		return (type.DeclaringMethod != null ? "``" : "`") + type.GenericParameterPosition;
	return (type.FullName ?? type.Name).Replace('+', '.');
}

string Declaration(Type type)
{
	string kind = type.IsEnum ? "enum" : type.IsValueType ? "struct" : type.IsInterface ? "interface"
		: type.IsAbstract && type.IsSealed ? "static class" : type.IsAbstract ? "abstract class" : "class";
	string declaration = $"public {kind} {type.Name}";
	if (!type.IsEnum && !type.IsValueType && type.BaseType != null && type.BaseType != typeof(object))
		declaration += " : " + TypeName(type.BaseType);
	return declaration;
}

string Signature(MethodBase method)
{
	var parts = new List<string> { "public" };
	if (method.IsFamily)
		parts[0] = "protected";
	if (method.IsStatic)
		parts.Add("static");
	if (method.IsVirtual && !method.IsFinal && method.DeclaringType!.IsClass)
		parts.Add(method.IsAbstract ? "abstract" : "virtual");
	string name = method is ConstructorInfo ? method.DeclaringType!.Name : method.Name;
	if (method is MethodInfo info)
	{
		if (method.Name.StartsWith("op_"))
			name = "operator " + OperatorSymbol(method.Name);
		parts.Add(TypeName(info.ReturnType) + (IsNullableReturn(info) ? "?" : ""));
	}
	string parameters = string.Join(", ", method.GetParameters().Select(Parameter));
	string constraints = "";
	if (method.IsGenericMethodDefinition)
	{
		Type[] arguments = method.GetGenericArguments();
		name += "<" + string.Join(", ", arguments.Select(a => a.Name)) + ">";
		foreach (Type argument in arguments)
		{
			var clauses = argument.GetGenericParameterConstraints().Select(TypeName).ToList();
			if (argument.GenericParameterAttributes.HasFlag(GenericParameterAttributes.DefaultConstructorConstraint))
				clauses.Add("new()");
			if (clauses.Count > 0)
				constraints += $" where {argument.Name} : {string.Join(", ", clauses)}";
		}
	}
	return $"{string.Join(' ', parts)} {name}({parameters}){constraints}";
}

string Parameter(ParameterInfo parameter)
{
	string text = $"{TypeName(parameter.ParameterType)}{(IsNullableParameter(parameter) ? "?" : "")} {parameter.Name}";
	if (parameter.HasDefaultValue)
	{
		object? value = parameter.DefaultValue;
		string literal = value switch
		{
			null => parameter.ParameterType.IsValueType ? "default" : "null",
			bool b => b ? "true" : "false",
			string s => $"\"{s}\"",
			float f => f.ToString(System.Globalization.CultureInfo.InvariantCulture) + "f",
			Enum e => $"{parameter.ParameterType.Name}.{e}",
			_ => Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture) ?? "default",
		};
		text += " = " + literal;
	}
	return text;
}

string TypeName(Type type)
{
	if (type.IsByRef)
		return "ref " + TypeName(type.GetElementType()!);
	if (type.IsArray)
		return TypeName(type.GetElementType()!) + "[]";
	if (type.IsGenericParameter)
		return type.Name;
	if (type.IsGenericType)
	{
		if (type.GetGenericTypeDefinition() == typeof(Nullable<>))
			return TypeName(type.GetGenericArguments()[0]) + "?";
		string name = type.Name[..type.Name.IndexOf('`')];
		return $"{name}<{string.Join(", ", type.GetGenericArguments().Select(TypeName))}>";
	}
	return type.FullName switch
	{
		"System.Void" => "void", "System.Boolean" => "bool", "System.Single" => "float", "System.Double" => "double",
		"System.Int32" => "int", "System.UInt32" => "uint", "System.Int64" => "long", "System.UInt64" => "ulong",
		"System.Int16" => "short", "System.UInt16" => "ushort", "System.Byte" => "byte", "System.String" => "string",
		"System.Object" => "object",
		_ => type.IsNested ? TypeName(type.DeclaringType!) + "." + type.Name : type.Name,
	};
}

string OperatorSymbol(string name) => name switch
{
	"op_Addition" => "+", "op_Subtraction" => "-", "op_Multiply" => "*", "op_Division" => "/",
	"op_UnaryNegation" => "-", "op_Equality" => "==", "op_Inequality" => "!=", "op_Implicit" => "implicit",
	"op_Explicit" => "explicit", _ => name,
};

// Nullable reference annotations (string?, Entity?) from the compiler's NullableAttribute metadata.
bool IsNullable(PropertyInfo property) => !property.PropertyType.IsValueType && new NullabilityInfoContext().Create(property).ReadState == NullabilityState.Nullable;
bool IsNullableReturn(MethodInfo method) => !method.ReturnType.IsValueType && method.ReturnType != typeof(void) && new NullabilityInfoContext().Create(method.ReturnParameter).ReadState == NullabilityState.Nullable;
bool IsNullableParameter(ParameterInfo parameter) => !parameter.ParameterType.IsValueType && new NullabilityInfoContext().Create(parameter).ReadState == NullabilityState.Nullable;

string Category(Type type)
{
	if (type.Name.EndsWith("Component") || type == typeof(object))
		return "Components";
	return type.Name switch
	{
		"Entity" or "Prefab" or "AssetRef" or "Mesh" or "StaticMesh" or "Material" or "Texture2D" or "Scene" => "Entities and assets",
		"Input" or "KeyCode" or "MouseButton" or "GamepadButton" or "GamepadAxis" => "Input",
		"Vector2" or "Vector3" or "Vector4" => "Math",
		"Log" or "Gizmos" => "Debugging",
		_ when type.Name.Contains("Audio") || type.Name.Contains("Dialogue") || type.Name.Contains("Music") || type.Name.Contains("Acoustic") || type.Name.Contains("Accessibility") || type.Name.Contains("Caption") => "Audio",
		_ when type.Name.EndsWith("Attribute") => "Attributes",
		_ => "Other",
	};
}

int CategoryOrder(string category) => Array.IndexOf(new[] { "Entities and assets", "Components", "Input", "Math", "Audio", "Debugging", "Attributes", "Other" }, category);
