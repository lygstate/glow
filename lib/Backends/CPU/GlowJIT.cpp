/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "GlowJIT.h"
namespace llvm {
namespace orc {

GlowJIT::GlowJIT(std::shared_ptr<TargetMachine> TM)
    : ES_(), TM_(TM), DL_(TM_->createDataLayout()), Resolvers_(),
      ObjectLayer_(ES_,
                   [this](VModuleKey K) {
                     return RTDyldObjectLinkingLayer::Resources{
                         std::make_shared<SectionMemoryManager>(),
                         Resolvers_[K]};
                   }),
      CompileLayer_(ObjectLayer_, SimpleCompiler(*TM_)) {
  llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
}

GlowJIT::~GlowJIT() {}

VModuleKey GlowJIT::addModule(std::unique_ptr<Module> M) {
  // Create a new VModuleKey.
  VModuleKey K = ES_.allocateVModule();

  // Build a resolver and associate it with the new key.
  Resolvers_[K] = createLegacyLookupResolver(
      ES_,
      [this](const std::string &Name) -> JITSymbol {
        if (auto Sym = CompileLayer_.findSymbol(Name, false))
          return Sym;
        else if (auto Err = Sym.takeError())
          return std::move(Err);
        if (auto SymAddr = RTDyldMemoryManager::getSymbolAddressInProcess(Name))
          return JITSymbol(SymAddr, JITSymbolFlags::Exported);
        return nullptr;
      },
      [](Error Err) { cantFail(std::move(Err), "lookupFlags failed"); });

  // Add the module to the JIT with the new key.
  cantFail(CompileLayer_.addModule(K, std::move(M)));
  return K;
}

llvm::JITSymbol GlowJIT::findSymbol(const std::string name) {
  std::string mangledName;
  raw_string_ostream MangledNameStream(mangledName);
  Mangler::getNameWithPrefix(MangledNameStream, name, DL_);
  return CompileLayer_.findSymbol(MangledNameStream.str(), true);
}

void GlowJIT::removeModule(VModuleKey K) {
  cantFail(CompileLayer_.removeModule(K));
}

} // end namespace orc
} // end namespace llvm
