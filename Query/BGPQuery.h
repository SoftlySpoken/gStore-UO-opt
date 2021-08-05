/*=============================================================================
# Filename: BGPQuery.h
# Author: Linglin Yang
# Mail: linglinyang@stu.pku.edu.cn
=============================================================================*/


// READ THIS FIRST
// According to need of rewriting, the whole BGP(including all triples) invokes EncodeBGPQuery,
// the split small BGP invokes EncodeSmallBGPQuery(the id is same as above, so maybe not begin with 0 or not continuous).
// For example, the whole query is:
// select ?s ?o ?o2 ?o3 where{
// 	?s p1 ?o.
// 	?s p2 ?o2.
// 	Optional{?s p3 ?o3}
// }
// Then after EncodeBGPQuery, the var-encode map is: {{?s:0}, {?o:1}, {?o2:2}, {?o3:3}},
// and in small optional BGP(?s p3 ?o3), the var-encode map is {{?s:0}, {?o3:3}}, not {{?s:0}, {?o3:1}}.

#ifndef GSTORE_BGPQUERY_H
#define GSTORE_BGPQUERY_H

#include "BasicQuery.h"
#include "../Util/Util.h"
#include "../Util/Triple.h"
#include "../KVstore/KVstore.h"

#include <cstdlib>
#include <utility>
#include <vector>


using namespace std;



class VarDescriptor{

public:
	// cannot descriminate literal with entity
	enum class VarType{Entity,Predicate};
	enum class EntiType{VarEntiType, ConEntiType};
	enum class PreType{VarPreType, ConPreType};

	VarType var_type_;
	std::string var_name_;
	bool selected_;

	// degree_ is the num of how many triples include this item,
	// include entitytype or pretype
	unsigned degree_;

	// only used when var_type_ == Entity
	unsigned so_var_var_edge_num;
	unsigned so_var_con_edge_num;

	// connect to var so, only used when var_type_ == Entity,
	// var_edge_pre_type_ records whether the pre is var or not
	vector<char> var_edge_type_;
	vector<unsigned > var_edge_index_;
	vector<unsigned > var_edge_nei_;
	vector<TYPE_PREDICATE_ID> var_edge_pre_id_;
	vector<PreType> var_edge_pre_type_;


	// connect to constant so, only used when var_type == Entity,
	// con_edge_pre_type_ records whether the pre is var or not
	vector<char> con_edge_type_;
	vector<unsigned > con_edge_index_;
	vector<unsigned > con_edge_nei_;
	vector<TYPE_PREDICATE_ID> con_edge_pre_id_;
	vector<PreType> con_edge_pre_type_;


	//pre_var edge info, only used when var_type == Predicate
	vector<unsigned> s_id_;
	vector<EntiType> s_type_;
	vector<unsigned> o_id_;
	vector<EntiType> o_type_;
	vector<unsigned> edge_index_;

	int rewriting_position_; //zhouyuqi
	// -1 if not allocated a id in BasicQuery
	int basic_query_id;

	// bool IsSatellite() const{return this->degree_ == 1 && (!selected_);};
	// int RewritingPosition(){return this->rewriting_position_;};
	// calculate the exact position in final result

	VarDescriptor(VarType var_type, const string &var_name);

	// VarDescriptor(VarType var_type,const std::string& var_name,BasicQuery* basic_query):
	// 		var_type_(var_type),var_name_(var_name)
	// {
	// 	this->basic_query_id = basic_query->getIDByVarName(var_name);
	// 	this->selected_ = basic_query->isVarSelected(var_name);
	// 	this->degree_ = basic_query->getVarDegree(basic_query_id);
	//
	// 	if(!this->selected_)
	// 		this->rewriting_position_ = -1;
	// 	// record_len = select_var_num + selected_pre_var_num;
	// 	if(this->var_type_ == VarType::EntityType)
	// 		this->rewriting_position_ = basic_query->getSelectedVarPosition(this->var_name_);
	//
	// 	if(this->var_type_ == VarType::PredicateType)
	// 		this->rewriting_position_ = basic_query->getSelectedPreVarPosition(this->var_name_);
	// };

	// bool get_edge_type(int edge_id);
	// int get_edge_nei(int edge_id);
	// int get_edge_index(int edge_id);

	void update_so_var_edge_info(unsigned edge_nei_id, TYPE_PREDICATE_ID pre_id, char edge_type,
								 unsigned edge_index, bool pre_is_var, bool edge_nei_is_var);

	void update_pre_var_edge_info(unsigned s_id, unsigned o_id, bool s_is_var, bool o_is_var, unsigned edge_index);

	void update_statistics_num();

	//TODO, default appoint a var selected_ = false to true
	void update_select_status(bool selected);
};


class SOVarDescriptor:public VarDescriptor{

};

// How to use this class?
// First, all triples need insert one by one by AddTriple
// Then, invoke the function EncodeBGPQuery, this will give every var(selected or not, predicate or not predicate) an id
// If you have special need to encode one small BGP, please invoke the function EncodeSmallBGPQuery,
// and pass big BGPQuery pointer to it, it will keep the var with the same string name having same.
class BGPQuery {
public:

	// all item, including s, p, o, whether var or not
	map<string, int> item_to_freq;
	// map var item to its position in var_vector
	map<string, unsigned > var_item_to_position;

	// all var, whether pre or not_pre, whether selected or not_selected
	vector<shared_ptr<VarDescriptor>> var_vector;

	// vector indicate so_var position in var_vector
	vector<unsigned> so_var_id;
	vector<unsigned> pre_var_id;

	// vector indicate selected var position in var_vector, whether pre_var or so_var
	vector<int> selected_var_id;

	vector<string> pre_var_names;

//	maybe not used
//	this record all pre_var position in item
//	vector<int> pre_var_position;

	int selected_pre_var_num;
	int selected_so_var_num;
	int total_selected_var_num;

	unsigned total_pre_var_num;
	unsigned total_so_var_num;
	unsigned total_var_num;

//	maybe not use join_pre_var_num
	int join_pre_var_num;
	int join_so_var_num;
	int total_join_var_num;


	BGPQuery();
	void initial();


	void AddTriple(const Triple& _triple);

	// return var_id in var_vector, if not find, return -1;
	unsigned get_var_id_by_name(const string& var_name);

	// void update_so_var_edge_info(uns);

	void ScanAllVar();

	void build_edge_info(KVstore *_kvstore);

	void count_statistics_num();

	bool EncodeBGPQuery(KVstore* _kvstore, const vector<string>& _query_var);
	bool EncodeSmallBGPQuery(BGPQuery *big_bgpquery_, KVstore* _kvstore, const vector<string>& _query_var);

	unsigned get_triple_num();
	unsigned get_total_var_num();
	unsigned get_pre_var_num();


	bool get_edge_type(int var_id, int edge_id);
	int get_edge_nei(int var_id, int edge_id);
	int get_edge_index(int var_id, int edge_id);


private:
	vector<Triple> triple_vt;


};


#endif //GSTORE_BGPQUERY_H