#include <sigmaengine/dapp_history/dapp_history_plugin.hpp>
#include <sigmaengine/dapp_history/dapp_impacted.hpp>
#include <sigmaengine/dapp_history/dapp_history_objects.hpp>
#include <sigmaengine/dapp_history/dapp_history_api.hpp>

#include <sigmaengine/chain/database.hpp>
#include <sigmaengine/chain/index.hpp>
#include <sigmaengine/chain/history_object.hpp>

namespace sigmaengine { namespace dapp_history {

   namespace detail {
      class dapp_history_plugin_impl
      {
         public:
            dapp_history_plugin_impl( dapp_history_plugin& _plugin ) : _self( _plugin ) {}

            sigmaengine::chain::database& database() {
               return _self.database();
            }
            void on_pre_operation( const operation_notification& note );

         private:
            dapp_history_plugin&  _self;
      };  //class dapp_history_plugin_impl

      struct operation_visitor {
         operation_visitor( database& db, const operation_notification& note, const operation_object*& n, dapp_name_type _name )
            :_db( db ), _note( note ), _new_obj( n ), dapp_name( _name ) {}

         typedef void result_type;

         database& _db;
         const operation_notification& _note;
         const operation_object*& _new_obj;
         dapp_name_type dapp_name;

         template<typename Op>
         void operator()( Op&& )const {
            if( !_new_obj ) {
               const auto& idx = _db.get_index< operation_index >().indices().get< by_location >();
               auto itr = idx.lower_bound( boost::make_tuple( _note.block, _note.trx_in_block, _note.op_in_trx, _note.virtual_op ) );
               if( itr != idx.end() && itr->block == _note.block 
                  && itr->trx_in_block == _note.trx_in_block && itr->op_in_trx == _note.op_in_trx
                  && itr->virtual_op == _note.virtual_op ) {
                  _new_obj = &( *itr );
               } else {
                  while( itr != idx.end() && itr->block == _note.block ){
                     if(itr->trx_in_block == _note.trx_in_block && itr->op_in_trx == _note.op_in_trx && itr->virtual_op == _note.virtual_op) {
                        _new_obj = &( *itr );
                        break;
                     }
                     itr++;
                  }
               }

               if( !_new_obj ) {
                  _new_obj = &_db.create< operation_object >( [&]( operation_object& object ) {
                     object.trx_id       = _note.trx_id;
                     object.block        = _note.block;
                     object.trx_in_block = _note.trx_in_block;
                     object.op_in_trx    = _note.op_in_trx;
                     object.virtual_op   = _note.virtual_op;
                     object.timestamp    = _db.head_block_time();
                     auto size = fc::raw::pack_size( _note.op );
                     object.serialized_op.resize( size );
                     fc::datastream< char* > ds( object.serialized_op.data(), size );
                     fc::raw::pack( ds, _note.op );
                  });
               }
            }

            const auto& hist_idx = _db.get_index< dapp_history_index >().indices().get< by_dapp_name >();
            auto hist_itr = hist_idx.lower_bound( boost::make_tuple( dapp_name, uint32_t(-1) ) );
            uint32_t sequence = 0;
            if( hist_itr != hist_idx.end() && hist_itr->dapp_name == dapp_name )
               sequence = hist_itr->sequence + 1;

            uint32_t all_sequence = 0;
            const auto & idx = _db.get_index< dapp_history_index >().indices().get< by_transaction >();
            auto itr = idx.lower_bound( uint32_t(-1) );
            if( itr != idx.end() )
            {
               all_sequence = itr->all_sequence + 1;
            }

            _db.create< dapp_history_object >( [&]( dapp_history_object& object ) {
               object.dapp_name  = dapp_name;
               object.sequence   = sequence;
               object.all_sequence   = all_sequence;
               object.op         = _new_obj->id;
            });


            if ( _note.op.which() == operation::tag<custom_json_dapp_operation>::value )
            {
               const std::string& create_type_name = fc::get_typename< nsta602_create_operation >::name();
               auto cstart = create_type_name.find_last_of( ':' ) + 1;
               auto cend   = create_type_name.find_last_of( '_' );
               string create_op_name  = create_type_name.substr( cstart, cend - cstart );
            
               const std::string& type_name = fc::get_typename< nsta602_transfer_operation >::name();
               auto start = type_name.find_last_of( ':' ) + 1;
               auto end   = type_name.find_last_of( '_' );
               string transfer_op_name  = type_name.substr( start, end - start );

               const std::string& extype_name = fc::get_typename< nsta602_extransfer_operation >::name();
               auto exstart = extype_name.find_last_of( ':' ) + 1;
               auto exend   = extype_name.find_last_of( '_' );
               string extransfer_op_name  = extype_name.substr( exstart, exend - exstart );

               const std::string& approvetype_name = fc::get_typename< nsta602_approve_operation >::name();
               auto approvestart = approvetype_name.find_last_of( ':' ) + 1;
               auto approveend   = approvetype_name.find_last_of( '_' );
               string approve_op_name  = approvetype_name.substr( approvestart, approveend - approvestart );
               
               auto& custom_op = _note.op.get< custom_json_dapp_operation >();
               try{
                  auto var = fc::json::from_string( custom_op.json );
                  auto ar = var.get_array();
                  if( ( ar[0].is_uint64() && ar[0].as_uint64() == dapp_operation::tag< nsta602_create_operation >::value ) 
                     || ( ar[0].as_string() == create_op_name ) ) 
                  {
                     const nsta602_create_operation inner_op = ar[1].as< nsta602_create_operation >();
                     nsta602_create_operation temp_op = inner_op;
                  
                     const auto& idx = _db.get_index< nsta602_transfer_history_index >().indices().get< by_nsta602 >();
                     auto itr = idx.lower_bound( boost::make_tuple( dapp_name, temp_op.author, temp_op.unique_id, uint32_t(-1) ) );
                     uint32_t sequence = 0;
                     if( itr != idx.end() && itr->dapp_name == dapp_name && itr->author == temp_op.author && to_string(itr->unique_id) == temp_op.unique_id )
                        sequence = itr->sequence + 1;

                     _db.create< nsta602_transfer_history_object >( [&]( nsta602_transfer_history_object& object ) {
                        object.dapp_name  = dapp_name;
                        object.author     = temp_op.author;
                        from_string(object.unique_id, temp_op.unique_id);
                        object.sequence   = sequence;
                        object.op         = _new_obj->id;
                     });
                      
                  }
                  else if( ( ar[0].is_uint64() && ar[0].as_uint64() == dapp_operation::tag< nsta602_transfer_operation >::value ) 
                     || ( ar[0].as_string() == transfer_op_name ) ) 
                  {
                     const nsta602_transfer_operation inner_op = ar[1].as< nsta602_transfer_operation >();
                     nsta602_transfer_operation temp_op = inner_op;
                  
                     const auto& idx = _db.get_index< nsta602_transfer_history_index >().indices().get< by_nsta602 >();
                     auto itr = idx.lower_bound( boost::make_tuple( dapp_name, temp_op.author, temp_op.unique_id, uint32_t(-1) ) );
                     uint32_t sequence = 0;
                     if( itr != idx.end() && itr->dapp_name == dapp_name && itr->author == temp_op.author && to_string(itr->unique_id) == temp_op.unique_id )
                        sequence = itr->sequence + 1;

                     _db.create< nsta602_transfer_history_object >( [&]( nsta602_transfer_history_object& object ) {
                        object.dapp_name  = dapp_name;
                        object.author     = temp_op.author;
                        from_string(object.unique_id, temp_op.unique_id);
                        object.sequence   = sequence;
                        object.op         = _new_obj->id;
                     });
                      
                  }
                  else if (( ar[0].is_uint64() && ar[0].as_uint64() == dapp_operation::tag< nsta602_extransfer_operation >::value ) 
                           || ( ar[0].as_string() == extransfer_op_name ) )
                  {
                     const nsta602_extransfer_operation inner_op = ar[1].as< nsta602_extransfer_operation >();
                     nsta602_extransfer_operation temp_op = inner_op;

                     const auto& idx = _db.get_index< nsta602_transfer_history_index >().indices().get< by_nsta602 >();
                     auto itr = idx.lower_bound( boost::make_tuple( dapp_name, temp_op.author, temp_op.unique_id, uint32_t(-1) ) );
                     uint32_t sequence = 0;
                     if( itr != idx.end() && itr->dapp_name == dapp_name && itr->author == temp_op.author && to_string(itr->unique_id) == temp_op.unique_id )
                        sequence = itr->sequence + 1;

                     _db.create< nsta602_transfer_history_object >( [&]( nsta602_transfer_history_object& object ) {
                        object.dapp_name  = dapp_name;
                        object.author     = temp_op.author;
                        from_string(object.unique_id, temp_op.unique_id);
                        object.sequence   = sequence;
                        object.op         = _new_obj->id;
                     });
                  }
                  else if (( ar[0].is_uint64() && ar[0].as_uint64() == dapp_operation::tag< nsta602_approve_operation >::value ) 
                           || ( ar[0].as_string() == approve_op_name ) )
                  {
                     const nsta602_approve_operation inner_op = ar[1].as< nsta602_approve_operation >();
                     nsta602_approve_operation temp_op = inner_op;

                     const auto& idx = _db.get_index< nsta602_transfer_history_index >().indices().get< by_nsta602 >();
                     auto itr = idx.lower_bound( boost::make_tuple( dapp_name, temp_op.author, temp_op.unique_id, uint32_t(-1) ) );
                     uint32_t sequence = 0;
                     if( itr != idx.end() && itr->dapp_name == dapp_name && itr->author == temp_op.author && to_string(itr->unique_id) == temp_op.unique_id )
                        sequence = itr->sequence + 1;

                     _db.create< nsta602_transfer_history_object >( [&]( nsta602_transfer_history_object& object ) {
                        object.dapp_name  = dapp_name;
                        object.author     = temp_op.author;
                        from_string(object.unique_id, temp_op.unique_id);
                        object.sequence   = sequence;
                        object.op         = _new_obj->id;
                     });
                  }
                  
               }
               catch( const fc::exception& ) {
                  
               }
            }

         
            
         }
      };  // struct operation_visitor

      void dapp_history_plugin_impl::on_pre_operation( const operation_notification& note ){
         flat_set< dapp_name_type > impacted;
         sigmaengine::chain::database& db = database();

         const operation_object* new_obj = nullptr;
         operation_get_impacted_dapp( note.op, db, impacted );

         for( const auto& dapp_name : impacted ) {
            note.op.visit( operation_visitor( db, note, new_obj, dapp_name ) );
         }
      }
   } //namespace detail

   dapp_history_plugin::dapp_history_plugin( application* app )
      : plugin( app ), _my( new detail::dapp_history_plugin_impl( *this ) ) {}

   void dapp_history_plugin::plugin_initialize( const boost::program_options::variables_map& options ) {
      try {
         ilog( "Intializing dapp history plugin" );

         chain::database& db = database();
         add_plugin_index< dapp_history_index >( db );
         add_plugin_index< nsta602_transfer_history_index >( db );

         db.pre_apply_operation.connect( [&]( const operation_notification& note ){ 
            _my->on_pre_operation(note); 
         });

      } FC_CAPTURE_AND_RETHROW()
   }

   void dapp_history_plugin::plugin_startup() {
      app().register_api_factory< dapp_history_api >( "dapp_history_api" );
   }

} } //namespace sigmaengine::dapp_history

SIGMAENGINE_DEFINE_PLUGIN( dapp_history, sigmaengine::dapp_history::dapp_history_plugin )


