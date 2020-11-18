//===------------ DependencyScanningTool.cpp - Swift Compiler -------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/DependencyScan/DependencyScanningTool.h"
#include "swift/DependencyScan/ModuleInfo.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsFrontend.h"
#include "swift/Basic/LLVMInitialize.h"
#include "swift/Frontend/Frontend.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"

#include <sstream>

namespace swift {
namespace dependencies {

DependencyScanningTool::DependencyScanningTool()
    : SharedCache(std::make_unique<ModuleDependenciesCache>()), PDC(), Alloc(),
      Saver(Alloc) {}

llvm::ErrorOr<std::string> DependencyScanningTool::getDependencies(
    ArrayRef<const char *> Command,
    const llvm::StringSet<> &PlaceholderModules) {
  // The primary instance used to scan the query Swift source-code
  auto InstanceOrErr = initCompilerInstanceForScan(Command);
  if (std::error_code EC = InstanceOrErr.getError())
    return EC;
  auto Instance = std::move(*InstanceOrErr);

  std::string JSONOutput;
  llvm::raw_string_ostream OSS(JSONOutput);
  performModuleScan(*Instance.get(), *SharedCache, OSS);
  OSS.flush();

  // TODO: swiftch to an in-memory representation
  return JSONOutput;
}

std::error_code DependencyScanningTool::getDependencies(
    ArrayRef<const char *> Command,
    const std::vector<BatchScanInput> &BatchInput,
    const llvm::StringSet<> &PlaceholderModules) {
  // The primary instance used to scan Swift modules
  auto InstanceOrErr = initCompilerInstanceForScan(Command);
  if (std::error_code EC = InstanceOrErr.getError())
    return EC;
  auto Instance = std::move(*InstanceOrErr);

  performBatchModuleScan(*Instance.get(), *SharedCache, Saver, BatchInput);
  return std::error_code();
}

llvm::ErrorOr<std::unique_ptr<CompilerInstance>>
DependencyScanningTool::initCompilerInstanceForScan(
    ArrayRef<const char *> Command) {
  // State unique to an individual scan
  auto Instance = std::make_unique<CompilerInstance>();
  Instance->addDiagnosticConsumer(&PDC);

  // Basic error checking on the arguments
  if (Command.empty()) {
    Instance->getDiags().diagnose(SourceLoc(), diag::error_no_frontend_args);
    return std::make_error_code(std::errc::invalid_argument);
  }

  CompilerInvocation Invocation;
  SmallString<128> WorkingDirectory;
  llvm::sys::fs::current_path(WorkingDirectory);

  // Parse arguments.
  std::string CommandString;
  for (const auto *c : Command) {
    CommandString.append(c);
    CommandString.append(" ");
  }
  SmallVector<const char *, 4> Args;
  llvm::cl::TokenizeGNUCommandLine(CommandString, Saver, Args);
  if (Invocation.parseArgs(Args, Instance->getDiags())) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  // Setup the instance
  Instance->setup(Invocation);
  (void)Instance->getMainModule();

  return Instance;
}

} // namespace dependencies
} // namespace swift
