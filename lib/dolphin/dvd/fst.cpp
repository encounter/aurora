#include "dvd.hpp"

#include <algorithm>

using namespace aurora::dvd::impl;

namespace {

struct OverlayFileEntry {
  std::string fileName;
  void* userData;
  u32 size;
  s32 entryNum;
};

std::vector<OverlayFileEntry> s_overlayFiles;


u32 fstCallback(u32 index, NodNodeKind kind, const char* name, u32 size, void* userData) {
  auto* ctx = static_cast<IterateContext*>(userData);

  while (index >= ctx->dirStack.back().second) {
    ctx->dirStack.pop_back();
  }

  const auto newEntry = std::make_shared<IterateNode>(
    name,
    (kind == NOD_NODE_KIND_DIRECTORY),
    size,
    index);

  const auto& curDir = ctx->dirStack.back().first;
  curDir->children.push_back(newEntry);

  if (newEntry->isDir) {
    ctx->dirStack.emplace_back(newEntry, size);
  }

  return index + 1;
}

IterateNode* findNode(const IterateNode& node, const std::string_view name) {
  for (const auto& child : node.children) {
    if (nameEqualsIgnoreCase(child->name, name)) {
      return child.get();
    }
  }

  return nullptr;
}

void mergeOverlayFileIntoContext(const IterateContext& context, const OverlayFileEntry& overlayFile) {
  IterateNode* node = context.root.get();
  std::string_view filePath = overlayFile.fileName;

  assert(filePath.starts_with('/'));
  filePath = filePath.substr(1);
  while (true) {
    const auto nextDelim = filePath.find('/');
    if (nextDelim == std::string_view::npos) {
      break;
    }

    const auto segment = filePath.substr(0, nextDelim);
    filePath = filePath.substr(nextDelim + 1);

    const auto existingNode = findNode(*node, segment);
    if (existingNode) {
      if (!existingNode->isDir) {
        Log.error("Overlay file {} needs directory that's already a file!", overlayFile.fileName);
        return;
      }

      node = existingNode;
    } else {
      const auto newNode = std::make_shared<IterateNode>(std::string(segment), true, 0, k_invalidFstEntry);
      node->children.push_back(newNode);
      node = newNode.get();
    }
  }

  // Remainder of fileName is the actual file name, and node is the directory we're in.

  auto newNode = IterateNode(std::string(filePath), false, overlayFile.size, overlayFile.entryNum, overlayFile.userData);
  const auto existingNode = findNode(*node, filePath);
  if (existingNode) {
    if (existingNode->isDir) {
      Log.error("Overlay file {} overlaps directory with same name!", overlayFile.fileName);
      return;
    }

    newNode.originalEntryNum = existingNode->originalEntryNum;

    // Replace existing disc entry.
    *existingNode = std::move(newNode);
  } else {
    // Add new entry.
    if (overlayFile.entryNum < s_baseEntryCount) {
      Log.error(
        "Overlay file {} has entryNum {} which is already used by the base disc!",
        overlayFile.fileName,
        overlayFile.entryNum);
      return;
    }

    newNode.originalEntryNum = overlayFile.entryNum;

    node->children.emplace_back(std::make_shared<IterateNode>(std::move(newNode)));
  }
}

void mergeOverlayFilesIntoContext(const IterateContext& context) {
  for (const auto& overlayFile : s_overlayFiles) {
    mergeOverlayFileIntoContext(context, overlayFile);
  }
}

void makeFstRecursive(IterateNode& node, u32 parent) {
  if (node.originalEntryNum != k_invalidFstEntry) {
    if (s_fstEntryMap.size() <= node.originalEntryNum) {
      s_fstEntryMap.resize(node.originalEntryNum + 1, k_invalidFstEntry);
    }

    auto& map = s_fstEntryMap[node.originalEntryNum];
    if (map != k_invalidFstEntry) {
      Log.error("File {} with virtual entry num {} already exists in map!", node.name, node.originalEntryNum);
      return;
    }

    map = static_cast<PhysicalEntryNum>(s_fstEntries.size());
  }

  if (!node.isDir) {
    assert(node.children.empty());
    assert(node.originalEntryNum != k_invalidFstEntry);

    s_fstEntries.emplace_back(node.name, false, parent, node.size, node.overlayData, node.isOverlay, node.originalEntryNum);
    return;
  }

  std::ranges::sort(node.children, [](const auto& a, const auto& b) { return a->name < b->name; });

  const auto ourIndex = s_fstEntries.size();
  s_fstEntries.emplace_back(node.name, true, parent, 0, node.overlayData, node.isOverlay, node.originalEntryNum);

  for (const auto& child : node.children) {
    makeFstRecursive(*child, ourIndex);
  }

  s_fstEntries[ourIndex].nextOrLength = s_fstEntries.size();
}

void makeFstFromContext(const IterateContext& context) {
  makeFstRecursive(*context.root, 0);
}

s32 calcEntryCount(const IterateNode& node) {
  s32 counter = 1;

  for (const auto& child : node.children) {
    counter += calcEntryCount(*child);
  }

  return counter;
}

bool validateOverlayFile(const AuroraOverlayFile& file) {
  const std::string_view name(file.fileName);

  if (!name.starts_with('/')) {
    Log.error("Overlay path {} does not start with /", name);
    return false;
  }

  if (file.size > std::numeric_limits<u32>::max()) {
    Log.error("Overlay file sizes above 4 GiB are not supported: {}", name);
    return false;
  }

  if (file.entryNum < 0) {
    Log.error("Overlay file entry is below zero: {}", name);
    return false;
  }

  return true;
}

}

namespace aurora::dvd::impl {

bool rebuildFST() {
  using namespace std::string_literals;

  if (s_partition == nullptr) {
    return false;
  }

  std::lock_guard lock(s_fstLock);

  // TODO: Ensure current dir still valid after rebuild.

  s_fstEntries.clear();
  s_fstEntryMap.clear();
  IterateContext ctx;
  ctx.root = std::make_shared<IterateNode>(""s, true, 0, static_cast<u32>(0));
  ctx.dirStack.emplace_back(ctx.root, std::numeric_limits<u32>::max());

  nod_partition_iterate_fst(s_partition, fstCallback, &ctx);
  s_baseEntryCount = calcEntryCount(*ctx.root);
  mergeOverlayFilesIntoContext(ctx);
  makeFstFromContext(ctx);

  return true;
}

bool nameEqualsIgnoreCase(const std::string_view lhs, const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < rhs.size(); ++i) {
    char lc = lhs[i];
    char rc = rhs[i];
    if (lc >= 'a' && lc <= 'z') {
      lc = static_cast<char>(lc - 'a' + 'A');
    }
    if (rc >= 'a' && rc <= 'z') {
      rc = static_cast<char>(rc - 'a' + 'A');
    }
    if (lc != rc) {
      return false;
    }
  }
  return true;
}

}

s32 aurora_dvd_base_entry_count() {
  return s_baseEntryCount;
}

void aurora_dvd_overlay_files(const AuroraOverlayFile* files, size_t nFiles) {
  if (!s_overlayCallbacksSet) {
    Log.fatal("aurora_dvd_overlay_callbacks not called before aurora_dvd_overlay_files!");
  }

  s_overlayFiles.clear();

  for (size_t i = 0; i < nFiles; i++) {
    const auto& file = files[i];

    if (!validateOverlayFile(file)) {
      continue;
    }

    s_overlayFiles.emplace_back(file.fileName, file.userData, static_cast<u32>(file.size), file.entryNum);
  }

  rebuildFST();
}

void aurora_dvd_overlay_callbacks(const AuroraOverlayCallbacks* callbacks) {
  s_overlayCallbacks = *callbacks;
  s_overlayCallbacksSet = true;
}
