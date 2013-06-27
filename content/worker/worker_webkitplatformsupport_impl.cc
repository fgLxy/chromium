// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/worker/worker_webkitplatformsupport_impl.h"

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/platform_file.h"
#include "base/strings/utf_string_conversions.h"
#include "content/child/database_util.h"
#include "content/child/fileapi/webfilesystem_impl.h"
#include "content/child/indexed_db/proxy_webidbfactory_impl.h"
#include "content/child/thread_safe_sender.h"
#include "content/child/webblobregistry_impl.h"
#include "content/child/webmessageportchannel_impl.h"
#include "content/common/file_utilities_messages.h"
#include "content/common/mime_registry_messages.h"
#include "content/worker/worker_thread.h"
#include "ipc/ipc_sync_message_filter.h"
#include "net/base/mime_util.h"
#include "third_party/WebKit/public/platform/WebBlobRegistry.h"
#include "third_party/WebKit/public/platform/WebFileInfo.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "webkit/glue/webfileutilities_impl.h"
#include "webkit/glue/webkit_glue.h"

using WebKit::Platform;
using WebKit::WebBlobRegistry;
using WebKit::WebClipboard;
using WebKit::WebFileInfo;
using WebKit::WebFileSystem;
using WebKit::WebFileUtilities;
using WebKit::WebMessagePortChannel;
using WebKit::WebMimeRegistry;
using WebKit::WebSandboxSupport;
using WebKit::WebStorageNamespace;
using WebKit::WebString;
using WebKit::WebURL;

namespace content {

// TODO(kinuko): Probably this could be consolidated into
// RendererWebKitPlatformSupportImpl::FileUtilities.
class WorkerWebKitPlatformSupportImpl::FileUtilities
    : public webkit_glue::WebFileUtilitiesImpl {
 public:
  explicit FileUtilities(ThreadSafeSender* sender)
      : thread_safe_sender_(sender) {}
  virtual bool getFileInfo(const WebString& path, WebFileInfo& result);
 private:
  scoped_refptr<ThreadSafeSender> thread_safe_sender_;
};

bool WorkerWebKitPlatformSupportImpl::FileUtilities::getFileInfo(
    const WebString& path,
    WebFileInfo& web_file_info) {
  base::PlatformFileInfo file_info;
  base::PlatformFileError status;
  if (!thread_safe_sender_.get() ||
      !thread_safe_sender_->Send(new FileUtilitiesMsg_GetFileInfo(
           base::FilePath::FromUTF16Unsafe(path), &file_info, &status)) ||
      status != base::PLATFORM_FILE_OK) {
    return false;
  }
  webkit_glue::PlatformFileInfoToWebFileInfo(file_info, &web_file_info);
  web_file_info.platformPath = path;
  return true;
}

//------------------------------------------------------------------------------

WorkerWebKitPlatformSupportImpl::WorkerWebKitPlatformSupportImpl(
    ThreadSafeSender* sender,
    IPC::SyncMessageFilter* sync_message_filter)
    : thread_safe_sender_(sender),
      child_thread_loop_(base::MessageLoopProxy::current()),
      sync_message_filter_(sync_message_filter) {
}

WorkerWebKitPlatformSupportImpl::~WorkerWebKitPlatformSupportImpl() {
}

WebClipboard* WorkerWebKitPlatformSupportImpl::clipboard() {
  NOTREACHED();
  return NULL;
}

WebMimeRegistry* WorkerWebKitPlatformSupportImpl::mimeRegistry() {
  return this;
}

WebFileSystem* WorkerWebKitPlatformSupportImpl::fileSystem() {
  if (!web_file_system_)
    web_file_system_.reset(new WebFileSystemImpl());
  return web_file_system_.get();
}

WebFileUtilities* WorkerWebKitPlatformSupportImpl::fileUtilities() {
  if (!file_utilities_) {
    file_utilities_.reset(new FileUtilities(thread_safe_sender_.get()));
    file_utilities_->set_sandbox_enabled(sandboxEnabled());
  }
  return file_utilities_.get();
}

WebSandboxSupport* WorkerWebKitPlatformSupportImpl::sandboxSupport() {
  NOTREACHED();
  return NULL;
}

bool WorkerWebKitPlatformSupportImpl::sandboxEnabled() {
  // Always return true because WebKit should always act as though the Sandbox
  // is enabled for workers.  See the comment in WebKitPlatformSupport for
  // more info.
  return true;
}

unsigned long long WorkerWebKitPlatformSupportImpl::visitedLinkHash(
    const char* canonical_url,
    size_t length) {
  NOTREACHED();
  return 0;
}

bool WorkerWebKitPlatformSupportImpl::isLinkVisited(
    unsigned long long link_hash) {
  NOTREACHED();
  return false;
}

WebMessagePortChannel*
WorkerWebKitPlatformSupportImpl::createMessagePortChannel() {
  return new WebMessagePortChannelImpl(child_thread_loop_.get());
}

void WorkerWebKitPlatformSupportImpl::setCookies(
    const WebURL& url,
    const WebURL& first_party_for_cookies,
    const WebString& value) {
  NOTREACHED();
}

WebString WorkerWebKitPlatformSupportImpl::cookies(
    const WebURL& url, const WebURL& first_party_for_cookies) {
  // WebSocketHandshake may access cookies in worker process.
  return WebString();
}

void WorkerWebKitPlatformSupportImpl::prefetchHostName(const WebString&) {
  NOTREACHED();
}

WebString WorkerWebKitPlatformSupportImpl::defaultLocale() {
  NOTREACHED();
  return WebString();
}

WebStorageNamespace*
WorkerWebKitPlatformSupportImpl::createLocalStorageNamespace(
    const WebString& path, unsigned quota) {
  NOTREACHED();
  return 0;
}

void WorkerWebKitPlatformSupportImpl::dispatchStorageEvent(
    const WebString& key, const WebString& old_value,
    const WebString& new_value, const WebString& origin,
    const WebKit::WebURL& url, bool is_local_storage) {
  NOTREACHED();
}

Platform::FileHandle
WorkerWebKitPlatformSupportImpl::databaseOpenFile(
    const WebString& vfs_file_name, int desired_flags) {
  return DatabaseUtil::DatabaseOpenFile(
      vfs_file_name, desired_flags, sync_message_filter_.get());
}

int WorkerWebKitPlatformSupportImpl::databaseDeleteFile(
    const WebString& vfs_file_name, bool sync_dir) {
  return DatabaseUtil::DatabaseDeleteFile(
      vfs_file_name, sync_dir, sync_message_filter_.get());
}

long WorkerWebKitPlatformSupportImpl::databaseGetFileAttributes(
    const WebString& vfs_file_name) {
  return DatabaseUtil::DatabaseGetFileAttributes(vfs_file_name,
                                                 sync_message_filter_.get());
}

long long WorkerWebKitPlatformSupportImpl::databaseGetFileSize(
    const WebString& vfs_file_name) {
  return DatabaseUtil::DatabaseGetFileSize(vfs_file_name,
                                           sync_message_filter_.get());
}

long long WorkerWebKitPlatformSupportImpl::databaseGetSpaceAvailableForOrigin(
    const WebString& origin_identifier) {
  return DatabaseUtil::DatabaseGetSpaceAvailable(origin_identifier,
                                                 sync_message_filter_.get());
}

WebKit::WebIDBFactory* WorkerWebKitPlatformSupportImpl::idbFactory() {
  if (!web_idb_factory_)
    web_idb_factory_.reset(new RendererWebIDBFactoryImpl(thread_safe_sender_));
  return web_idb_factory_.get();
}

WebMimeRegistry::SupportsType
WorkerWebKitPlatformSupportImpl::supportsMIMEType(
    const WebString&) {
  return WebMimeRegistry::IsSupported;
}

WebMimeRegistry::SupportsType
WorkerWebKitPlatformSupportImpl::supportsImageMIMEType(
    const WebString&) {
  NOTREACHED();
  return WebMimeRegistry::IsSupported;
}

WebMimeRegistry::SupportsType
WorkerWebKitPlatformSupportImpl::supportsJavaScriptMIMEType(const WebString&) {
  NOTREACHED();
  return WebMimeRegistry::IsSupported;
}

WebMimeRegistry::SupportsType
WorkerWebKitPlatformSupportImpl::supportsMediaMIMEType(
    const WebString&, const WebString&) {
  NOTREACHED();
  return WebMimeRegistry::IsSupported;
}

WebMimeRegistry::SupportsType
WorkerWebKitPlatformSupportImpl::supportsMediaMIMEType(
    const WebString&, const WebString&, const WebString&) {
  NOTREACHED();
  return WebMimeRegistry::IsSupported;
}

bool WorkerWebKitPlatformSupportImpl::supportsMediaSourceMIMEType(
    const WebKit::WebString& mimeType, const WebKit::WebString& codecs) {
  NOTREACHED();
  return false;
}

WebMimeRegistry::SupportsType
WorkerWebKitPlatformSupportImpl::supportsNonImageMIMEType(
    const WebString&) {
  NOTREACHED();
  return WebMimeRegistry::IsSupported;
}

WebString WorkerWebKitPlatformSupportImpl::mimeTypeForExtension(
    const WebString& file_extension) {
  std::string mime_type;
  thread_safe_sender_->Send(new MimeRegistryMsg_GetMimeTypeFromExtension(
      base::FilePath::FromUTF16Unsafe(file_extension).value(), &mime_type));
  return ASCIIToUTF16(mime_type);
}

WebString WorkerWebKitPlatformSupportImpl::wellKnownMimeTypeForExtension(
    const WebString& file_extension) {
  std::string mime_type;
  net::GetWellKnownMimeTypeFromExtension(
      base::FilePath::FromUTF16Unsafe(file_extension).value(), &mime_type);
  return ASCIIToUTF16(mime_type);
}

WebString WorkerWebKitPlatformSupportImpl::mimeTypeFromFile(
    const WebString& file_path) {
  std::string mime_type;
  thread_safe_sender_->Send(
      new MimeRegistryMsg_GetMimeTypeFromFile(
          base::FilePath::FromUTF16Unsafe(file_path),
          &mime_type));
  return ASCIIToUTF16(mime_type);
}

WebString WorkerWebKitPlatformSupportImpl::preferredExtensionForMIMEType(
    const WebString& mime_type) {
  base::FilePath::StringType file_extension;
  thread_safe_sender_->Send(
      new MimeRegistryMsg_GetPreferredExtensionForMimeType(
          UTF16ToASCII(mime_type), &file_extension));
  return base::FilePath(file_extension).AsUTF16Unsafe();
}

WebBlobRegistry* WorkerWebKitPlatformSupportImpl::blobRegistry() {
  if (!blob_registry_.get() && thread_safe_sender_.get())
    blob_registry_.reset(new WebBlobRegistryImpl(thread_safe_sender_.get()));
  return blob_registry_.get();
}

}  // namespace content
