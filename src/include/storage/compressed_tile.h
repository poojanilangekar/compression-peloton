//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// compressed_tile.h
//
// Identification: src/include/storage/compressed_tile.h
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <mutex>

#include "catalog/manager.h"
#include "catalog/schema.h"
#include "common/item_pointer.h"
#include "common/printable.h"
#include "type/abstract_pool.h"
#include "type/serializeio.h"
#include "type/serializer.h"
#include "type/value_factory.h"
#include "storage/tile.h"

namespace peloton {

namespace gc {
class GCManager;
}

namespace storage {

//===--------------------------------------------------------------------===//
// CompressedTile
//===--------------------------------------------------------------------===//

class Tuple;
class TileGroup;
class TileGroupHeader;
class TupleIterator;
class Tile;

/**
 * Represents a CompressedTile.
 *
 * CompressedTiles are only instantiated via TileGroup.
 * TileGroup compresses existing tile on a per Tile basis.
 *
 * NOTE: MVCC is implemented on the shared TileGroupHeader.
 */
class CompressedTile : public Tile {
  CompressedTile(CompressedTile const &) = delete;
  CompressedTile() = delete;

 public:
  // Constructor
  CompressedTile(BackendType backend_type, TileGroupHeader *tile_header,
                 const catalog::Schema &tuple_schema, TileGroup *tile_group,
                 int tuple_count);

  // Destructor
  ~CompressedTile();

  //===--------------------------------------------------------------------===//
  // Operations
  //===--------------------------------------------------------------------===//

  void CompressTile(Tile *tile);

  type::Value GetMaxExponentLength(Tile *tile, oid_t column_id);

  std::vector<type::Value> ConvertDecimalColumn(Tile *tile, oid_t column_id,
                                                type::Value exponent);

  std::vector<type::Value> GetIntegerColumnValues(Tile *tile, oid_t column_id);

  std::vector<type::Value> CompressColumn(
      Tile *tile, oid_t column_id, std::vector<type::Value> column_values,
      type::Value &base_value, type::Type::TypeId &compression_type);

  std::vector<type::Value> CompressCharColumn(Tile *tile, oid_t column_id);

  void InsertTuple(const oid_t tuple_offset, Tuple *tuple);

  type::Value GetValue(const oid_t tuple_offset, const oid_t column_id);

  type::Value GetValueFast(const oid_t tuple_offset, const size_t column_offset,
                           const type::Type::TypeId column_type,
                           const bool is_inlined);

  void SetValue(const type::Value &value, const oid_t tuple_offset,
                const oid_t column_id);

  void SetValueFast(const type::Value &value, const oid_t tuple_offset,
                    const size_t column_offset, const bool is_inlined,
                    const size_t column_length);

  type::Value GetUncompressedVarcharValue(oid_t column_id,
                                          type::Value compressed_value);
  //===--------------------------------------------------------------------===//
  // Utility Functions
  //===--------------------------------------------------------------------===//

  inline bool IsCompressed() { return is_compressed; }

  inline type::Type::TypeId GetCompressedType(type::Value new_value) {
    return new_value.GetTypeId();
  }

  inline void SetCompressedMapValue(oid_t column_id, type::Type::TypeId type_id,
                                    type::Value base_value) {
    compressed_column_map[column_id] = std::make_pair(type_id, base_value);
  }

  inline void SetExponentMapValue(oid_t column_id, type::Value exponent) {
    exponent_column_map[column_id] = exponent;
  }

  // this is for dictionary
  inline void SetDecoderMapValue(oid_t column_id,
                                 std::vector<type::Value> decoder) {
    decoder_map[column_id] = decoder;
  }

  inline type::Value GetBaseValue(oid_t column_id) {
    if (compressed_column_map.find(column_id) != compressed_column_map.end()) {
      return compressed_column_map[column_id].second;
    }
    return type::Value();
  }

  inline type::Type::TypeId GetCompressedType(oid_t column_id) {
    if (compressed_column_map.find(column_id) != compressed_column_map.end()) {
      return compressed_column_map[column_id].first;
    }
    return type::Type::INVALID;
  }

  type::Value GetUncompressedValue(oid_t column_id,
                                   type::Value compressed_value);

  oid_t GetColumnFromOffset(const size_t column_offset) {
    return column_offset_map[column_offset];
  }

 protected:
  //===--------------------------------------------------------------------===//
  // Data members
  //===--------------------------------------------------------------------===//

  bool is_compressed;
  oid_t compressed_columns_count;
  int tuple_count;

  std::map<size_t, oid_t> column_offset_map;

  std::map<oid_t, std::pair<type::Type::TypeId, type::Value>>
      compressed_column_map;
  std::map<oid_t, type::Value> exponent_column_map;
  std::map<oid_t, std::vector<type::Value>> decoder_map;
};
}
}
