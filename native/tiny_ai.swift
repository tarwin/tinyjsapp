// tiny.ai — a thin C-callable shim over Apple's on-device FoundationModels
// LLM (macOS 26+, Apple Intelligence). Compiled with swiftc into an object
// and linked into the launcher ONLY when built with TINYJS_AI=1 (which needs
// the macOS 26 SDK); the default build never references these symbols, so it
// still compiles on older SDKs / CI. See setup.sh.
//
// The launcher calls tiny_ai_generate from a background dispatch queue and
// blocks on the result — generation takes seconds, so never call it on the
// UI thread. Returned strings are strdup'd; the caller frees them.

import Foundation
import FoundationModels

// 1 = ready, 0 = model exists but unavailable (Apple Intelligence off / not
// downloaded / device unsupported), -1 = OS too old.
@_cdecl("tiny_ai_available")
public func tiny_ai_available() -> Int32 {
  if #available(macOS 26.0, *) {
    switch SystemLanguageModel.default.availability {
    case .available: return 1
    default: return 0
    }
  }
  return -1
}

// Blocking generate. prompt + optional instructions (system prompt) in;
// returns the completion (caller frees), or NULL with *errOut set on failure.
@_cdecl("tiny_ai_generate")
public func tiny_ai_generate(
  _ prompt: UnsafePointer<CChar>,
  _ instructions: UnsafePointer<CChar>?,
  _ errOut: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?
) -> UnsafeMutablePointer<CChar>? {
  guard #available(macOS 26.0, *) else {
    errOut?.pointee = strdup("needs macOS 26")
    return nil
  }
  let p = String(cString: prompt)
  let instr = instructions.map { String(cString: $0) }

  let sem = DispatchSemaphore(value: 0)
  var out: String?
  var err: String?
  Task {
    do {
      let session = instr.map { LanguageModelSession(instructions: $0) }
        ?? LanguageModelSession()
      let response = try await session.respond(to: p)
      out = response.content
    } catch {
      err = String(describing: error)
    }
    sem.signal()
  }
  sem.wait()

  if let out { return strdup(out) }
  errOut?.pointee = strdup(err ?? "generation failed")
  return nil
}

// ---------------------------------------------------------------------------
// Tool calling. The tools are declared in JAVASCRIPT, not here, so the schema
// can't be a compile-time @Generable type — DynamicGenerationSchema builds one
// from values instead, and the tool takes untyped GeneratedContent.
//
// When the model calls a tool, `call` hands the name + JSON arguments back to
// the launcher through a C function pointer and waits for the result. That C
// side blocks until the JS handler answers, so the hop runs on a global queue
// rather than on the Swift concurrency pool — blocking a cooperative thread is
// how you deadlock this.

@available(macOS 26.0, *)
struct TinyDynamicTool: Tool {
  typealias Arguments = GeneratedContent
  var name: String
  var description: String
  var parameters: GenerationSchema
  var invoke: @Sendable (String, String) -> String    // (name, argsJson) -> result

  func call(arguments: GeneratedContent) async throws -> String {
    let payload = String(describing: arguments)
    let toolName = name
    let fn = invoke
    return await withCheckedContinuation { cont in
      DispatchQueue.global().async { cont.resume(returning: fn(toolName, payload)) }
    }
  }
}

// One JSON property -> one dynamic schema node. Deliberately small: string,
// number/integer and boolean cover what a tool argument realistically is, and
// anything unknown becomes a string so a typo degrades instead of throwing.
@available(macOS 26.0, *)
private func nodeFor(_ type: String) -> DynamicGenerationSchema {
  switch type {
  case "number", "float", "double": return DynamicGenerationSchema(type: Double.self)
  case "integer", "int": return DynamicGenerationSchema(type: Int.self)
  case "boolean", "bool": return DynamicGenerationSchema(type: Bool.self)
  default: return DynamicGenerationSchema(type: String.self)
  }
}

@available(macOS 26.0, *)
private func toolsFrom(_ json: String,
                       _ invoke: @escaping @Sendable (String, String) -> String)
    -> [any Tool] {
  guard let data = json.data(using: .utf8),
        let specs = (try? JSONSerialization.jsonObject(with: data)) as? [[String: Any]]
  else { return [] }
  var tools: [any Tool] = []
  for spec in specs {
    guard let name = spec["name"] as? String else { continue }
    let desc = spec["description"] as? String ?? name
    let props = spec["parameters"] as? [String: Any] ?? [:]
    var fields: [DynamicGenerationSchema.Property] = []
    for (key, raw) in props {
      let info = raw as? [String: Any] ?? ["type": raw]
      let type = info["type"] as? String ?? "string"
      fields.append(DynamicGenerationSchema.Property(
        name: key,
        description: info["description"] as? String ?? key,
        schema: nodeFor(type)))
    }
    let root = DynamicGenerationSchema(name: name + "Args", description: desc,
                                       properties: fields)
    guard let built = try? GenerationSchema(root: root, dependencies: []) else { continue }
    tools.append(TinyDynamicTool(name: name, description: desc,
                                 parameters: built, invoke: invoke))
  }
  return tools
}

// Same contract as tiny_ai_generate, plus a tools JSON array and the C callback
// the tools reach back through. `invoke` must return a malloc'd string the
// caller frees, or NULL for "no result".
@_cdecl("tiny_ai_generate_tools")
public func tiny_ai_generate_tools(
  _ prompt: UnsafePointer<CChar>,
  _ instructions: UnsafePointer<CChar>?,
  _ toolsJson: UnsafePointer<CChar>?,
  _ invoke: @convention(c) (UnsafePointer<CChar>?, UnsafePointer<CChar>?) -> UnsafeMutablePointer<CChar>?,
  _ errOut: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?
) -> UnsafeMutablePointer<CChar>? {
  guard #available(macOS 26.0, *) else {
    errOut?.pointee = strdup("needs macOS 26")
    return nil
  }
  let p = String(cString: prompt)
  let instr = instructions.map { String(cString: $0) }
  let specs = toolsJson.map { String(cString: $0) } ?? "[]"

  let bridge: @Sendable (String, String) -> String = { name, args in
    guard let raw = invoke(name, args) else { return "" }
    let out = String(cString: raw)
    free(raw)
    return out
  }
  let tools = toolsFrom(specs, bridge)

  let sem = DispatchSemaphore(value: 0)
  var out: String?
  var err: String?
  Task {
    do {
      let session = tools.isEmpty
        ? (instr.map { LanguageModelSession(instructions: $0) } ?? LanguageModelSession())
        : (instr.map { LanguageModelSession(tools: tools, instructions: $0) }
           ?? LanguageModelSession(tools: tools))
      let response = try await session.respond(to: p)
      out = response.content
    } catch {
      err = String(describing: error)
    }
    sem.signal()
  }
  sem.wait()

  if let out { return strdup(out) }
  errOut?.pointee = strdup(err ?? "generation failed")
  return nil
}
