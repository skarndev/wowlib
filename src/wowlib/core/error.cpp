#include <wowlib/core/error.hpp>

namespace wowlib
{
  std::string_view to_string(ErrorCode code)
  {
    switch (code)
    {
      case ErrorCode::StorageOpenFailed: return "StorageOpenFailed";
      case ErrorCode::ArchiveOpenFailed: return "ArchiveOpenFailed";
      case ErrorCode::StorageNotOpen: return "StorageNotOpen";
      case ErrorCode::FileNotFound: return "FileNotFound";
      case ErrorCode::PathNotResolvable: return "PathNotResolvable";
      case ErrorCode::FdidNotResolvable: return "FdidNotResolvable";
      case ErrorCode::ListfileParseError: return "ListfileParseError";
      case ErrorCode::ListfileIoError: return "ListfileIoError";
      case ErrorCode::FdidSpaceExhausted: return "FdidSpaceExhausted";
      case ErrorCode::DuplicatePath: return "DuplicatePath";
      case ErrorCode::InvalidPath: return "InvalidPath";
      case ErrorCode::IoError: return "IoError";
      case ErrorCode::EncryptedContent: return "EncryptedContent";
      case ErrorCode::NotSupported: return "NotSupported";
      case ErrorCode::NotImplemented: return "NotImplemented";
      case ErrorCode::BackendError: return "BackendError";
    }
    return "UnknownError";
  }
}