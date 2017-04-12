//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// compressed_tile.cpp
//
// Identification: src/storage/compressed_tile.cpp
//
// Copyright (c) 2015-16, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <sstream>

#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "type/serializer.h"
#include "type/types.h"
#include "type/ephemeral_pool.h"
#include "concurrency/transaction_manager_factory.h"
#include "storage/storage_manager.h"
#include "storage/tile.h"
#include "storage/tile_group_header.h"
#include "storage/tuple.h"
#include "storage/tuple_iterator.h"
#include "storage/compressed_tile.h"

namespace peloton {
namespace storage {

bool CompareLessThanBool(type::Value left, type::Value right) {
  return left.CompareLessThan(right);
}

CompressedTile::CompressedTile(BackendType backend_type, TileGroupHeader *tile_header,
           const catalog::Schema &tuple_schema, TileGroup *tile_group,
           int tuple_count) 
: Tile(backend_type, tile_header, tuple_schema, tile_group, tuple_count) 
{
  is_compressed = false; // tile is currently uncompressed.
  compressed_columns_count = 0;
}

CompressedTile::~CompressedTile() {
  std::cout<<"Compressed tile destructor\n";

}

void CompressedTile::CompressTile(Tile *tile) {

  auto allocated_tuple_count = tile->GetAllocatedTupleCount();
  std::vector<type::Value> new_column_values (allocated_tuple_count);
  std::vector<std::vector<type::Value>> new_columns (column_count);
  std::vector<catalog::Column> columns;
  auto tile_schema = tile->GetSchema();

  for(oid_t i = 0; i < column_count; i++) {
    std::cout<< tile_schema->GetColumn(i).GetName()<<"\n";
    switch(tile_schema->GetType(i)) {

      case type::Type::SMALLINT: 
      case type::Type::INTEGER:
      case type::Type::BIGINT:
        std::cout<<"Column Type is : "<< tile_schema->GetType(i)<< "\n";
        new_column_values = CompressColumn(tile, i, type::Type::TINYINT);
        if (new_column_values.size()!=0) {
          compressed_columns_count += 1;
          std::cout << "Compressed values for column " << tile_schema->GetColumn(i).GetName() << " : SUCCESS\n";

          type::Value old_value = tile->GetValue(0, i);
          type::Value new_value = new_column_values[0];

          type::Value base_value = GetBaseValue(old_value, new_value);
          type::Type::TypeId type_id = GetCompressedType(new_value);

          SetCompressedMapValue(i, type_id, base_value);

        } else {
          std::cout << "Compressed values for column " << tile_schema->GetColumn(i).GetName() << " : FAILURE\n";
        }
        new_columns[i] = new_column_values;
        break;
      
      default:
        std::cout<<"Currently uncompressable.\n";
        new_column_values.resize(0);
        new_columns[i] = new_column_values;
        break;
    }
  }

  if (compressed_columns_count != 0) {
      for (oid_t i = 0; i < column_count; i++) {
        // Get Column Info
        auto tile_column = tile_schema->GetColumn(i);
        // Individual Column info
        auto column_type = tile_column.GetType();
        auto column_type_size = tile_column.GetLength();
        auto column_name = tile_column.GetName();
        bool column_is_inlined = tile_column.IsInlined();

        if (new_columns[i].size() == 0) {
          catalog::Column column(column_type, column_type_size, column_name, column_is_inlined);
          std::cout<< "Pushing uncompressed column as is into the schema \t" << column_name << "\n";
          columns.push_back(column);
          } else {
            type::Type::TypeId new_column_type = GetCompressedType(i);
            catalog::Column column(new_column_type, type::Type::GetTypeSize(new_column_type),
                                column_name, column_is_inlined);
            std::cout<< "Pushing Compressed column into the schema \t" << column_name << "\n";
            std::cout<< "OLD TYPE of column is " << column_type << "\n";
            std::cout<< "NEW TYPE of column is " << new_column_type << "\n";
            columns.push_back(column);
          }
      }
      
       // reclaim the tile memory (INLINED data)
      auto &storage_manager = storage::StorageManager::GetInstance();
      
      //Reclaim data from old schema
      storage_manager.Release(backend_type, data);
      data = NULL;

      std::cout<<"Old tile size = "<< tile_size<<"\n";
      schema = catalog::Schema(columns);
      std::cout<<"Created new schema "<< schema.GetInfo()<<"\n";
      std::cout<<"Old tuple_length = "<< tuple_length <<"\n";
      tuple_length = schema.GetLength();
      std::cout<<"New tuple_length = "<< tuple_length <<"\n";
      tile_size = num_tuple_slots * tuple_length;
      std::cout<<"New tile_size =" << tile_size <<"\n";

      data = reinterpret_cast<char *>(storage_manager.Allocate(backend_type, tile_size));
      PL_ASSERT(data != NULL);
      // zero out the data
      PL_MEMSET(data, 0, tile_size);
      
      for (oid_t i = 0; i < column_count; i++) {
        bool is_inlined = schema.IsInlined(i);
        size_t new_column_offset = schema.GetOffset(i);

        if(GetCompressedType(i) == type::Type::INVALID) {
          size_t old_column_offset = tile_schema->GetOffset(i);
          auto old_column_type = tile_schema->GetColumn(i).GetType();

          for (oid_t j = 0; j < num_tuple_slots; j++ ) {
            type::Value old_value = tile-> GetValueFast(j, old_column_offset, old_column_type, is_inlined);
            Tile::SetValueFast(old_value, j, new_column_offset, old_column_type, is_inlined);
          }

        } else {

          auto new_column_type = schema.GetColumn(i).GetType();
          
          for (oid_t j = 0; j < num_tuple_slots; j++ ) {
            Tile::SetValueFast(new_columns[i][j], j, new_column_offset, new_column_type, is_inlined);
          }

        }
      }

      is_compressed = true;
      std::cout<< "Compressed Tile Details: \n";
      std::cout<<GetInfo()<<"\n";
    }

}


std::vector<type::Value> CompressedTile::CompressColumn(Tile* tile, oid_t column_id, type::Type::TypeId compression_type = type::Type::TINYINT) {
  oid_t num_tuples = tile->GetAllocatedTupleCount();
  auto tile_schema = tile->GetSchema();
  bool is_inlined = tile_schema->IsInlined(column_id);
  size_t column_offset = tile_schema->GetOffset(column_id);
  auto column_info = tile_schema->GetColumn(column_id);
  auto column_name = column_info.GetName();
  auto column_type = column_info.GetType();
  std::vector<type::Value> column_values(num_tuples);

  for(oid_t i = 0; i < num_tuples; i++) {
    column_values[i] = tile->GetValueFast(i, column_offset, column_type, is_inlined);
  }

  std::vector<type::Value> actual_values(column_values);

  std::cout<< "Sorting " << column_name <<"\n";
  std::sort(column_values.begin(), column_values.end(), CompareLessThanBool);
  std::cout<<"Sorted values \n";
  for(oid_t j = 0; j < num_tuples; j++) {
    std::cout<< column_values[j]<<"\t";
  }

  std::cout<<"\nMedian value = "<< column_values[num_tuples/2]<<"\n";
  std::cout<<"Minimum value = "<< column_values[0]<<"\n";
  std::cout<<"Maximum value = "<< column_values[num_tuples-1]<<"\n";


  type::Value median = column_values[num_tuples/2];
  std::vector<type::Value> modified_values(num_tuples);

  while(true) {
    try {
      type::Value min_diff = column_values[0].Subtract(median).CastAs(compression_type);
      type::Value max_diff = column_values[num_tuples-1].Subtract(median).CastAs(compression_type);
      for (oid_t k = 0; k < num_tuples; k++) {
        modified_values[k] = actual_values[k].Subtract(median).CastAs(compression_type);
        std::cout<< modified_values[k]<<"\t";
      }
      std::cout<<"\n";

      break;
    } catch(Exception &e) {
      std::cout<<"Can not compress to: "<< compression_type << "\n";
      if(column_type == (compression_type + 1)) {
        modified_values.resize(0);
        break;
      }
      compression_type = static_cast< type::Type::TypeId >(static_cast<int>(compression_type) + 1);
      
    }
  }
  return modified_values;
}

void CompressedTile::InsertTuple(const oid_t tuple_offset, Tuple *tuple) {

  if(IsCompressed()) {
    LOG_INFO("Peloton does not support insert into compressed tiles.");
    PL_ASSERT(false);
  } else {
    Tile::InsertTuple(tuple_offset, tuple);
  }
}


type::Value CompressedTile::GetValue(const oid_t tuple_offset, const oid_t column_id) {


  type::Value deserizedValue = Tile::GetValue(tuple_offset, column_id);

  if(!IsCompressed()) {
    return deserizedValue;
  }

  if(GetCompressedType(column_id) == type::Type::INVALID) {
    return deserizedValue;
  }

  type::Type::TypeId compressed_type = GetCompressedType(column_id);

  PL_ASSERT(compressed_type != type::Type::INVALID);

  type::Value original = GetUncompressedValue(column_id, deserizedValue);

  return original;
}


 type::Value CompressedTile::GetValueFast(const oid_t tuple_offset, const size_t column_offset,
                           const type::Type::TypeId column_type,
                           const bool is_inlined) {

  type::Value deserizedValue = Tile::GetValueFast(tuple_offset, column_offset, column_type, is_inlined);

  if(!IsCompressed()) {
    return deserizedValue;
  }

  oid_t column_id = GetColumnFromOffsest(column_offset);

  PL_ASSERT(column_id < column_count);

  if(GetCompressedType(column_id) != type::Type::INVALID) {
    type::Value original = GetUncompressedValue(column_id, deserizedValue);
    return original;
  }

  return deserizedValue;

 }


 void CompressedTile::SetValue(const type::Value &value, const oid_t tuple_offset,
                const oid_t column_id) {
  if(IsCompressed()) {
    if(GetCompressedType(column_id) != type::Type::INVALID) {
      LOG_INFO("Peloton does not support SetValue on a Compressed Class");
      PL_ASSERT(false);
    }
  }

  Tile::SetValue(value, tuple_offset, column_id);

 }
 
 void CompressedTile::SetValueFast(const type::Value &value, const oid_t tuple_offset,
                    const size_t column_offset, const bool is_inlined,
                    const size_t column_length) {
      
    oid_t column_id = GetColumnFromOffsest(column_offset);

    PL_ASSERT(column_id < column_count);

    if(IsCompressed()) {
      if(GetCompressedType(column_id) != type::Type::INVALID) {
        LOG_INFO("Peloton does not support");
        PL_ASSERT(false);
      }
    }

    Tile::SetValueFast(value, tuple_offset, column_offset, is_inlined, column_length);
 }



}
}
