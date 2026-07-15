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
