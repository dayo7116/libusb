#pragma once
#include <memory>
#include <string>
#include <vector>

//namespace MTXR {
//
//  enum class MediaType {
//    Video = 1,
//    Audio = 2,
//  };
//  
//  enum class DataType {
//    Text = 1,
//    Binary = 2,
//  };
//
//  class DataListener {
//  public:
//    virtual void OnData(MediaType media_type, DataType data_type,
//                        unsigned char* data, int size) = 0;
//  };
//
//  class DataObject {
//  public:
//    virtual ~DataObject() = default;
//
//    virtual MediaType GetMediaType() = 0;
//    virtual DataType GetDataType() = 0;
//
//    virtual bool GetText(std::string&) = 0;
//    virtual std::unique_ptr< std::vector<unsigned char> > GetBinary() = 0;
//
//  };
//
//  class XRDataClient {
//  public:
//    virtual ~XRDataClient() {
//    }
//
//    virtual void Start(std::shared_ptr<DataListener> processor) = 0;
//  };
//
//}
