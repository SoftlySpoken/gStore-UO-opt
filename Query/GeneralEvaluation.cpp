/*=============================================================================
# Filename: GeneralEvaluation.cpp
# Author: Jiaqi, Chen
# Mail: chenjiaqi93@163.com
# Last Modified: 2017-05-05
# Description: implement functions in GeneralEvaluation.h
=============================================================================*/

#include "GeneralEvaluation.h"
#include<set>
using namespace std;

#define TEST_BGPQUERY

GeneralEvaluation::EvaluationStackStruct::EvaluationStackStruct()
{
	// result = new TempResultSet();
	result = NULL;
}

GeneralEvaluation::EvaluationStackStruct::EvaluationStackStruct(const EvaluationStackStruct& that)
{
	result = new TempResultSet();
	group_pattern = that.group_pattern;
	if (that.result)
		*result = *(that.result);
}

GeneralEvaluation::EvaluationStackStruct& GeneralEvaluation::EvaluationStackStruct::operator=(const EvaluationStackStruct& that)
{
	TempResultSet *local_result = new TempResultSet();
	if (that.result)
		*local_result = *(that.result);
	if (result)
		delete result;
	result = local_result;
	group_pattern = that.group_pattern;

	return *this;
}

GeneralEvaluation::EvaluationStackStruct::~EvaluationStackStruct()
{
	// if (result)
	// 	delete result;
}

void *preread_from_index(void *argv)
{
	vector<StringIndexFile*> * indexfile = (vector<StringIndexFile*> *)*(long*)argv;
	ResultSet *ret_result = (ResultSet*)*((long*)argv + 1);
	vector<int>* proj2temp = (vector<int>*)*((long*)argv + 2);
	int id_cols = *((long*)argv + 3);
	TempResult* result0 = (TempResult*)*((long*)argv + 4);
	vector<bool>* isel = (vector<bool>*)*((long*)argv + 5);
	StringIndexFile* 	entity = (*indexfile)[0];
	StringIndexFile* 	literal = (*indexfile)[1];
	StringIndexFile* 	predicate = (*indexfile)[2];
	unsigned total = ret_result->ansNum;
	int var_num = ret_result->select_var_num;
	unsigned thelta = total / 4096;
	char tmp;
	char *entityc = entity->Mmap;
	char *literalc = literal->Mmap;
	char *prec = predicate->Mmap;
	for (unsigned i = 0; i < total; i ++)
	{
		for (int j = 0; j < var_num; j++)
		{
			int k = (*proj2temp)[j];
			long offset = -1;
			if (k != -1)
			{
				if (k < id_cols)
				{
					unsigned ans_id = result0->result[i].id[k];
					if (ans_id != INVALID)
					{
						if ((*isel)[k])
						{
							if (ans_id < Util::LITERAL_FIRST_ID)
							{
								offset = entity->GetOffsetbyID(ans_id);
								if (offset != -1)
								{
									tmp = entityc[offset];
								}
							}
							else
							{
								offset = literal->GetOffsetbyID(ans_id - Util::LITERAL_FIRST_ID);
								if (offset != -1)
								{
									tmp = literalc[offset];
								}
							}
						}
						else
						{
							offset = predicate->GetOffsetbyID(ans_id);	
							if (offset != -1)
							{
								tmp = prec[offset];
							}
						}
					}
				}
			}
		}
	}
	
	return NULL;
}


GeneralEvaluation::GeneralEvaluation(KVstore *_kvstore, Statistics *_statistics, StringIndex *_stringindex, QueryCache *_query_cache, \
	TYPE_TRIPLE_NUM *_pre2num,TYPE_TRIPLE_NUM *_pre2sub, TYPE_TRIPLE_NUM *_pre2obj, \
	TYPE_TRIPLE_NUM _triples_num, TYPE_PREDICATE_ID _limitID_predicate, TYPE_ENTITY_LITERAL_ID _limitID_literal, \
	TYPE_ENTITY_LITERAL_ID _limitID_entity, CSR *_csr, shared_ptr<Transaction> _txn):
	kvstore(_kvstore), statistics(_statistics), stringindex(_stringindex), query_cache(_query_cache), pre2num(_pre2num), \
	pre2sub(_pre2sub), pre2obj(_pre2obj), triples_num(_triples_num), limitID_predicate(_limitID_predicate), limitID_literal(_limitID_literal), \
	limitID_entity(_limitID_entity), temp_result(NULL), fp(NULL), export_flag(false), csr(_csr), txn(_txn)
{
	if (csr)
		pqHandler = new PathQueryHandler(csr);
	else
		pqHandler = NULL;
	well_designed = -1;	// Not checked yet
	this->optimizer_ = make_shared<Optimizer>(kvstore,statistics,pre2num,pre2sub,pre2obj,triples_num,limitID_predicate,
                                      limitID_literal,limitID_entity,txn);
	this->bgp_query_total = make_shared<BGPQuery>();
}

GeneralEvaluation::~GeneralEvaluation()
{
	if (pqHandler)
		delete pqHandler;
}

void
GeneralEvaluation::loadCSR()
{
	cout << "GeneralEvaluation::loadCSR" << endl;

	if (csr)
		delete csr;
	csr = new CSR[2];

	unsigned pre_num = stringindex->getNum(StringIndexFile::Predicate);
	csr[0].init(pre_num);
	csr[1].init(pre_num);	
	cout << "pre_num: " << pre_num << endl;
	long begin_time = Util::get_cur_time();

	// Process out-edges (csr[0])
	// i: predicate; j: subject; k: object
	for(unsigned i = 0; i < pre_num; i++)
	{
		string pre = kvstore->getPredicateByID(i);
		cout<<"pid: "<<i<<"    pre: "<<pre<<endl;
		unsigned* sublist = NULL;
		unsigned sublist_len = 0;
		kvstore->getsubIDlistBypreID(i, sublist, sublist_len, true);
		//cout<<"    sub_num: "<<sublist_len<<endl;
		unsigned offset = 0;
		unsigned index = 0;
		for(unsigned j=0;j<sublist_len;j++)
		{
			string sub = kvstore->getEntityByID(sublist[j]);
			//cout<<"    sid: "<<sublist[j]<<"    sub: "<<sub<<endl;
			unsigned* objlist = NULL;
			unsigned objlist_len = 0;
			kvstore->getobjIDlistBysubIDpreID(sublist[j], i, objlist, objlist_len); 
			unsigned len = objlist_len;	// the real object list length
			for(unsigned k=0;k<objlist_len;k++)
			{
				if(objlist[k]>=Util::LITERAL_FIRST_ID)
				{
					--len;
					continue;
				}
				csr[0].adjacency_list[i].push_back(objlist[k]);
			}
			if(len > 0)
			{
				csr[0].id2vid[i].push_back(sublist[j]);
				csr[0].vid2id[i].insert(pair<unsigned, unsigned>(sublist[j], index));
				csr[0].offset_list[i].push_back(offset);
				index++;
				offset += len;
			}
		}
		cout<<csr[0].offset_list[i].size()<<endl;	// # of this predicate's subjects
		cout<<csr[0].adjacency_list[i].size()<<endl;	// # of this predicate's objects
	}

	// Process out-edges (csr[1])
	// i: predicate; j: object; k: subject
	for(unsigned i = 0;i<pre_num;i++)
	{
		string pre = kvstore->getPredicateByID(i);
		cout<<"pid: "<<i<<"    pre: "<<pre<<endl;
		unsigned* objlist = NULL;
		unsigned objlist_len = 0;
		kvstore->getobjIDlistBypreID(i, objlist, objlist_len, true);
		//cout<<"    obj_num: "<<objlist_len<<endl;
		unsigned offset = 0;
		unsigned index = 0;
		for(unsigned j=0;j<objlist_len;j++)
		{
			if(objlist[j]>=Util::LITERAL_FIRST_ID)
				continue;
			string obj = kvstore->getEntityByID(objlist[j]);
			unsigned* sublist = NULL;
			unsigned sublist_len = 0;
			kvstore->getsubIDlistByobjIDpreID(objlist[j], i, sublist, sublist_len); 
			unsigned len = sublist_len;
			for(unsigned k=0;k<sublist_len;k++)
			{
				string sub = kvstore->getEntityByID(sublist[k]);
				csr[1].adjacency_list[i].push_back(sublist[k]);
			}
			if(len > 0)
			{
				csr[1].id2vid[i].push_back(objlist[j]);
				csr[1].vid2id[i].insert(pair<unsigned, unsigned>(objlist[j], index));
				csr[1].offset_list[i].push_back(offset);
				index++;
				offset += len;
			}
		}
		cout<<csr[1].offset_list[i].size()<<endl;
		cout<<csr[1].adjacency_list[i].size()<<endl;
	}
	long end_time = Util::get_cur_time();
	cout << "Loading CSR in GeneralEvaluation takes " << (end_time - begin_time) << "ms" << endl;
}

void
GeneralEvaluation::prepPathQuery()
{
	if (!csr)
		loadCSR();
	if (!pqHandler)
		pqHandler = new PathQueryHandler(csr);
}

void
GeneralEvaluation::setStringIndexPointer(StringIndex* _tmpsi)
{
	this->stringindex = _tmpsi;
}

bool GeneralEvaluation::parseQuery(const string &_query)
{
	try
	{
		this->query_parser.setQueryTree(&(this->query_tree));
		this->query_parser.SPARQLParse(_query);
	}
	catch (const char *e)
	{
		printf("%s\n", e);
		return false;
	}
	catch(const runtime_error& e2)
	{
		//cout<<"catch the runtime error"<<endl;
		throw runtime_error(e2.what());
		return false;
	}
	catch (...)
	{
		cout<<"GeneralEvaluation::parseQuery "<<" catch some error."<<endl;
		throw  runtime_error("Some syntax errors in sparql");
		return false;
	}

	return true;
}

bool GeneralEvaluation::rewriteQuery()
{
	printf("==========Original Query Tree==========\n");
	query_tree.print();

	combineBGPunfoldUnion(query_tree.getGroupPattern());

	printf("==========After combineBGPunfoldUnion Query Tree==========\n");
	query_tree.print();

	Varset useful = this->query_tree.getResultProjectionVarset() + this->query_tree.getGroupByVarset() \
				+ this->query_tree.getOrderByVarset();
	highLevelOpt(query_tree.getGroupPattern(), useful);

	printf("==========After highLevelOpt Query Tree==========\n");
	query_tree.print();
	return true;
}

int GeneralEvaluation::combineBGPunfoldUnion(QueryTree::GroupPattern &group_pattern)
{
	// Recursive call across children
	// SOLVED: for (auto sgp: group_pattern.sub_group_pattern) will cause sgp to be empty
	// Don't know why yet, but use index loop instead
	for (size_t i = 0; i < group_pattern.sub_group_pattern.size(); i++)
	{
		if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Group_type)
			combineBGPunfoldUnion(group_pattern.sub_group_pattern[i].group_pattern);
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Union_type)
		{
			int child_ret;
			size_t j = 0, processed = 0, originalSize = group_pattern.sub_group_pattern[i].unions.size();
			while (processed != originalSize)
			{
				child_ret = combineBGPunfoldUnion(group_pattern.sub_group_pattern[i].unions[j]);
				if (child_ret == 1)
				{
					for (size_t k = 0; k < group_pattern.sub_group_pattern[i].unions[j].sub_group_pattern[0].unions.size(); k++)
						group_pattern.sub_group_pattern[i].unions.emplace_back( \
							group_pattern.sub_group_pattern[i].unions[j].sub_group_pattern[0].unions[k]);
					group_pattern.sub_group_pattern[i].unions.erase(group_pattern.sub_group_pattern[i].unions.begin() + j);
				}
				else
					j++;

				processed++;
			}
		}
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Optional_type \
			|| group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Minus_type)
			combineBGPunfoldUnion(group_pattern.sub_group_pattern[i].optional);
	}

	// Unfold UNION
	if (group_pattern.sub_group_pattern.size() == 1 \
		&& group_pattern.sub_group_pattern[0].type == QueryTree::GroupPattern::SubGroupPattern::Union_type)
		return 1;

	// Combine BGP
	// TODO: do we need to put all the BGP nodes on the left? (just push_back for now)
	group_pattern.initPatternBlockid();
	for (size_t i = 0; i < group_pattern.sub_group_pattern.size(); i++)
	{
		if (group_pattern.sub_group_pattern[i].type != QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
			continue;

		size_t st = i;
		while (st > 0 && (group_pattern.sub_group_pattern[st - 1].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type \
			|| group_pattern.sub_group_pattern[st - 1].type == QueryTree::GroupPattern::SubGroupPattern::Union_type))
			st--;

		for (size_t j = st; j < i; j++)
		{
			if (group_pattern.sub_group_pattern[j].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
			{
				// If connected
				if (group_pattern.sub_group_pattern[i].pattern.subject_object_varset.hasCommonVar(\
					group_pattern.sub_group_pattern[j].pattern.subject_object_varset))
					group_pattern.mergePatternBlockID(i, j);
			}
		}
	}

	vector<size_t> merged;
	for (size_t i = 0; i < group_pattern.sub_group_pattern.size(); i++)
	{
		if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
		{
			int root = group_pattern.getRootPatternBlockID(i);
			if (group_pattern.sub_group_pattern[root].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
			{
				group_pattern.sub_group_pattern[root].type = QueryTree::GroupPattern::SubGroupPattern::BGP_type;
				group_pattern.sub_group_pattern[root].patterns.push_back(group_pattern.sub_group_pattern[root].pattern);
				if (root != i)
					group_pattern.sub_group_pattern[root].patterns.push_back(group_pattern.sub_group_pattern[i].pattern);
			}
			else if (group_pattern.sub_group_pattern[root].type == QueryTree::GroupPattern::SubGroupPattern::BGP_type)
				group_pattern.sub_group_pattern[root].patterns.push_back(group_pattern.sub_group_pattern[i].pattern);

			// Remove this SubGroupPattern if merged
			if (root != i)
				merged.push_back(i);
		}
	}
	for (size_t i = 0; i < merged.size(); i++)
		group_pattern.sub_group_pattern.erase(group_pattern.sub_group_pattern.begin() + merged[i] - i);

	return 0;
}

bool GeneralEvaluation::highLevelOpt(QueryTree::GroupPattern &group_pattern, Varset useful)
{
	// Construct varset to pass down (add all vars from this levels' triples and filters)
	Varset passDown = useful;
	for (size_t i = 0; i < group_pattern.sub_group_pattern.size(); i++)
	{
		if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::BGP_type)
		{
			for (size_t j = 0; j < group_pattern.sub_group_pattern[i].patterns.size(); j++)
				passDown += group_pattern.sub_group_pattern[i].patterns[j].varset;
		}
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Filter_type)
			passDown += group_pattern.sub_group_pattern[i].filter.varset;
	}

	// Recursive call across children
	for (size_t i = 0; i < group_pattern.sub_group_pattern.size(); i++)
	{
		if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Group_type)
			highLevelOpt(group_pattern.sub_group_pattern[i].group_pattern, passDown);
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Union_type)
		{
			for (size_t j = 0; j < group_pattern.sub_group_pattern[i].unions.size(); j++)
				highLevelOpt(group_pattern.sub_group_pattern[i].unions[j], passDown);
		}
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Optional_type \
			|| group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Minus_type)
			highLevelOpt(group_pattern.sub_group_pattern[i].optional, passDown);
	}

	// Construct varset of this level (add all vars from this level's OPTIONAL and filters)
	for (size_t i = 0; i < group_pattern.sub_group_pattern.size(); i++)
	{
		if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Optional_type)
		{
			// Call getVarset for optional again, since recursive call above might've changed it
			group_pattern.sub_group_pattern[i].optional.getVarset();
			useful += group_pattern.sub_group_pattern[i].optional.group_pattern_resultset_maximal_varset;
		}
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Filter_type)
			useful += group_pattern.sub_group_pattern[i].filter.varset;
	}

	// Extract and save MQO info
	vector<vector<QueryTree::GroupPattern::SubGroupPattern> > allMultiBGP(group_pattern.sub_group_pattern.size());
	vector<vector<QueryTree::GroupPattern::Pattern> > allSubBGP(group_pattern.sub_group_pattern.size());
	for (size_t i = 0; i < group_pattern.sub_group_pattern.size(); i++)
	{
		if (group_pattern.sub_group_pattern[i].type != QueryTree::GroupPattern::SubGroupPattern::Union_type)
			continue;

		// Gather BGPs for MQO
		const QueryTree::GroupPattern::SubGroupPattern &unionNode = group_pattern.sub_group_pattern[i];
		for (size_t j = 0; j < unionNode.unions.size(); j++)
		{
			for (size_t k = 0; k < unionNode.unions[j].sub_group_pattern.size(); k++)
			{
				if (unionNode.unions[j].sub_group_pattern[k].type \
					== QueryTree::GroupPattern::SubGroupPattern::BGP_type)
				{
					allMultiBGP[i].emplace_back(unionNode.unions[j].sub_group_pattern[k]);
					break;
					// TODO: Now if multiple BGPs in a UNION'd graph pattern, only select the first one
					// But more generally, common subquery may occur later, need to backtrack
				}
			}
		}

		// Extract common sub-BGP
		// Do not consider those without BGP directly under any UNION group graph pattern
		if (allMultiBGP[i].size() == unionNode.unions.size())
			extractLargestCommonSubBGP(allMultiBGP[i], allSubBGP[i]);
	}

	// Decide if push down BGP to UNION / OPTIONAL
	vector<size_t> merged;
	for (size_t i = 0; i < group_pattern.sub_group_pattern.size(); i++)
	{
		if (group_pattern.sub_group_pattern[i].type != QueryTree::GroupPattern::SubGroupPattern::BGP_type)
			continue;

		// Save the estimated cost of pushing down BGP to UNION (4 plans for each UNION)
		vector<vector<long long> > plansCost(group_pattern.sub_group_pattern.size());
		for (size_t j = 0; j < group_pattern.sub_group_pattern.size(); j++)
			plansCost[j].assign(4, LLONG_MAX);

		bool firstUnion = true;
		for (size_t j = 0; j < group_pattern.sub_group_pattern.size(); j++)
		{
			if (group_pattern.sub_group_pattern[j].type == QueryTree::GroupPattern::SubGroupPattern::Union_type)
			{
				unionCostModel(group_pattern.sub_group_pattern[i], group_pattern.sub_group_pattern[j], \
					allMultiBGP[j], allSubBGP[j], useful, plansCost[j]);
				if (firstUnion)
				{
					// Prevent re-estimating the original cost
					for (size_t k = 0; k < group_pattern.sub_group_pattern.size(); k++)
						plansCost[k][0] = plansCost[j][0];
					firstUnion = false;
				}
			}
		}

		// Select from plans
		long long minCost = LLONG_MAX;
		size_t target_union = -1;	// Index of UNION to push down BGP
		size_t target_plan = -1;	// Index of the push down plan
		for (size_t j = 0; j < group_pattern.sub_group_pattern.size(); j++)
		{
			if (group_pattern.sub_group_pattern[j].type == QueryTree::GroupPattern::SubGroupPattern::Union_type)
			{
				for (size_t k = 0; k < 4; k++)
				{
					if (plansCost[j][k] < minCost)
					{
						minCost = plansCost[j][k];
						target_union = j;
						target_plan = k;
					}
				}
			}
		}

		// TODO: could useful varset be problematic because of the change?
		if (target_union != -1 && target_plan != 0)
		{
			// Push BGP down to UNION
			// Modify Query Tree accordingly, and write the result size onto the current root node
			cout << "Push BGP down to UNION" << endl;
			cout << "target_union = " << target_union << ", target_plan = " << target_plan << endl;
			QueryTree::GroupPattern::SubGroupPattern &unionNode = group_pattern.sub_group_pattern[target_union];
			if (target_plan == 1)
			{
				// Plan 1: A {B} UNION {C} = {AB} UNION {AC}
				for (size_t j = 0; j < unionNode.unions.size(); j++)
				{
					bool hasBGP = false;
					for (size_t k = 0; k < unionNode.unions[j].sub_group_pattern.size(); k++)
					{
						if (unionNode.unions[j].sub_group_pattern[k].type \
							== QueryTree::GroupPattern::SubGroupPattern::BGP_type
							&& isBGPconnected(group_pattern.sub_group_pattern[i].patterns, \
							unionNode.unions[j].sub_group_pattern[k].patterns))
						{
							// Push down outerBGP into this BGP
							Util::vectorSum<QueryTree::GroupPattern::Pattern>(\
								unionNode.unions[j].sub_group_pattern[k].patterns, group_pattern.sub_group_pattern[i].patterns);
							hasBGP = true;
							break;
						}
					}
					// If this UNION'd graph pattern does not have connected BGP, directly add the BGP node
					if (!hasBGP)
						unionNode.unions[j].sub_group_pattern.emplace_back(group_pattern.sub_group_pattern[i]);
				}
				merged.emplace_back(i);
			}
			else if (target_plan == 2)
			{
				// Plan 2: A {B} UNION {C} = AD {B\D} UNION {C\D}
				// D = allSubBGP[target_union]
				for (size_t j = 0; j < unionNode.unions.size(); j++)
				{
					for (size_t k = 0; k < unionNode.unions[j].sub_group_pattern.size(); k++)
					{
						if (unionNode.unions[j].sub_group_pattern[k].type \
							== QueryTree::GroupPattern::SubGroupPattern::BGP_type)
						{
							Util::vectorSubtract<QueryTree::GroupPattern::Pattern>(\
								unionNode.unions[j].sub_group_pattern[k].patterns, allSubBGP[target_union]);
							break;
						}
					}
				}
				Util::vectorSum<QueryTree::GroupPattern::Pattern>(\
					group_pattern.sub_group_pattern[i].patterns, allSubBGP[target_union]);
			}
			else if (target_plan == 3)
			{
				// Plan 3: A {B} UNION {C} = D {AB\D} UNION {AC\D}
				for (size_t j = 0; j < unionNode.unions.size(); j++)
				{
					bool hasBGP = false;
					for (size_t k = 0; k < unionNode.unions[j].sub_group_pattern.size(); k++)
					{
						if (unionNode.unions[j].sub_group_pattern[k].type \
							== QueryTree::GroupPattern::SubGroupPattern::BGP_type)
						{
							Util::vectorSubtract<QueryTree::GroupPattern::Pattern>(\
								unionNode.unions[j].sub_group_pattern[k].patterns, allSubBGP[target_union]);
							if (isBGPconnected(group_pattern.sub_group_pattern[i].patterns, \
								unionNode.unions[j].sub_group_pattern[k].patterns))
							{
								Util::vectorSum<QueryTree::GroupPattern::Pattern>(\
									unionNode.unions[j].sub_group_pattern[k].patterns, group_pattern.sub_group_pattern[i].patterns);
								hasBGP = true;
								break;
							}
						}
					}
					// If this UNION'd graph pattern does not have connected BGP, directly add the BGP node
					if (!hasBGP)
						unionNode.unions[j].sub_group_pattern.emplace_back(group_pattern.sub_group_pattern[i]);
				}
				group_pattern.sub_group_pattern[i].patterns = allSubBGP[target_union];
			}
		}
		else
		{
			// Consider all the OPTIONALs on the right (unlike UNION, can be pushed down multiple times)
			for (size_t j = 0; j < group_pattern.sub_group_pattern.size(); j++)
				plansCost[j].assign(2, LLONG_MAX);
			vector<size_t> target_optional;
			bool firstOptional = true;
			for (size_t j = 0; j < group_pattern.sub_group_pattern.size(); j++)
			{
				if (group_pattern.sub_group_pattern[j].type == QueryTree::GroupPattern::SubGroupPattern::Optional_type)
				{
					optionalCostModel(group_pattern.sub_group_pattern[i], group_pattern.sub_group_pattern[j], useful, plansCost[j]);
					if (firstOptional)
					{
						// Prevent re-estimating the original cost
						for (size_t k = 0; k < group_pattern.sub_group_pattern.size(); k++)
							plansCost[k][0] = plansCost[j][0];
						firstOptional = false;
					}

					if (plansCost[j][0] > plansCost[j][1])
					{
						// Push BGP down to OPTIONAL
						// Modify Query Tree accordingly, and write the result size onto the current root node
						cout << "Push BGP down to OPTIONAL" << endl;
						for (size_t k = 0; k < group_pattern.sub_group_pattern[j].optional.sub_group_pattern.size(); k++)
						{
							if (group_pattern.sub_group_pattern[j].optional.sub_group_pattern[k].type \
								== QueryTree::GroupPattern::SubGroupPattern::BGP_type)
							{
								Util::vectorSum<QueryTree::GroupPattern::Pattern>(\
									group_pattern.sub_group_pattern[j].optional.sub_group_pattern[k].patterns, \
									group_pattern.sub_group_pattern[i].patterns);
								break;
							}
						}
					}
				}
			}
			// Else, nothing happens
		}
	}
	// Remove the completely pushed down BGP nodes
	for (size_t i = 0; i < merged.size(); i++)
		group_pattern.sub_group_pattern.erase(group_pattern.sub_group_pattern.begin() + merged[i] - i);

	// TODO: for UNIONs untouched by any BGP, can do stand-alone MQO

	// TODO: fill current group pattern resSz

	return true;
}

void GeneralEvaluation::optionalCostModel(const QueryTree::GroupPattern::SubGroupPattern &outerBGP, \
	const QueryTree::GroupPattern::SubGroupPattern &optionalNode, Varset useful, vector<long long> &cost)
{
	cout << "=====GeneralEvaluation::optionalCostModel=====" << endl;

	if (cost.capacity() < 2)
		cost.reserve(2);
	
	// If there is no connected BGP in this OPTIONAL, directly return
	size_t firstBGP = -1;
	for (size_t i = 0; i < optionalNode.optional.sub_group_pattern.size(); i++)
	{
		if (optionalNode.optional.sub_group_pattern[i].type \
			== QueryTree::GroupPattern::SubGroupPattern::BGP_type \
			&& isBGPconnected(outerBGP.patterns, optionalNode.optional.sub_group_pattern[i].patterns))
		{
			firstBGP = i;
			break;
			// TODO: Now if multiple BGPs in an OPTIONAL graph pattern, only select the first connected one
			// But more generally, common subquery may occur later, need to backtrack
		}
	}
	if (firstBGP == -1)
		return;

	// All the small BGP queries encode with intersection with useful
	Varset encode_varset;
	bool curr_exist = true;	// Used to store the result of EncodeSmallBGPQuery. If false, cost = res = 0

	if (cost[0] == LLONG_MAX)
	{
		auto bgp_query_inner = make_shared<BGPQuery>(optionalNode.optional.sub_group_pattern[firstBGP].patterns);
		getEncodeVarset(encode_varset, useful, optionalNode.optional.sub_group_pattern[firstBGP].patterns);
		curr_exist = bgp_query_inner->EncodeSmallBGPQuery(bgp_query_total.get(), kvstore, encode_varset.vars, \
			this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);
		pair<long long, long long> est_inner;
		if (curr_exist)
			est_inner = optimizer_->get_est_for_BGP(bgp_query_inner);
		else
			est_inner = make_pair(0, 0);
		cost[0] = est_inner.second;
		cout << "Plan 0:" << endl;
		cout << "bgp_query_inner:" << endl;
		for (size_t i = 0; i < bgp_query_inner->get_triple_vt().size(); i++)
			cout << bgp_query_inner->get_triple_vt()[i].subject << " " \
				<< bgp_query_inner->get_triple_vt()[i].predicate << " " \
				<< bgp_query_inner->get_triple_vt()[i].object << endl;
		cout << "est_inner: " << est_inner.first << " " << est_inner.second << endl;
	}

	vector<QueryTree::GroupPattern::Pattern> tmp_patterns;
	Util::vectorSum<QueryTree::GroupPattern::Pattern>(outerBGP.patterns, \
		optionalNode.optional.sub_group_pattern[firstBGP].patterns, tmp_patterns);
	auto bgp_query_combine = make_shared<BGPQuery>(tmp_patterns);
	getEncodeVarset(encode_varset, useful, tmp_patterns);
	curr_exist = bgp_query_combine->EncodeSmallBGPQuery(bgp_query_total.get(), kvstore, encode_varset.vars, \
		this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);
	pair<long long, long long> est_combine;
	if (curr_exist)
		est_combine = optimizer_->get_est_for_BGP(bgp_query_combine);
	else
		est_combine = make_pair(0, 0);
	cost[1] = est_combine.second;
	cout << "Plan 1:" << endl;
	cout << "bgp_query_combine:" << endl;
	for (size_t i = 0; i < bgp_query_combine->get_triple_vt().size(); i++)
		cout << bgp_query_combine->get_triple_vt()[i].subject << " " \
			<< bgp_query_combine->get_triple_vt()[i].predicate << " " \
			<< bgp_query_combine->get_triple_vt()[i].object << endl;
	cout << "est_combine: " << est_combine.first << " " << est_combine.second << endl;

	// TODO: fill OPTIONAL group graph pattern resSz
}

void GeneralEvaluation::unionCostModel(const QueryTree::GroupPattern::SubGroupPattern &outerBGP, \
	const QueryTree::GroupPattern::SubGroupPattern &unionNode, \
	const vector<QueryTree::GroupPattern::SubGroupPattern> &multiBGP, \
	const vector<QueryTree::GroupPattern::Pattern> &subBGP, \
	Varset useful, vector<long long> &cost)
{
	cout << "=====GeneralEvaluation::unionCostModel=====" << endl;

	// Compute and save costs (cost_bgp, cost_alg)

	// All the small BGP queries encode with intersection with useful
	Varset encode_varset;
	bool curr_exist = true;	// Used to store the result of EncodeSmallBGPQuery. If false, cost = res = 0
	if (cost.capacity() < 4)
		cost.reserve(4);

	size_t bgp_idx = 0;
	vector<bool> hasBGP(unionNode.unions.size());
	long long res_partial;
	vector<long long> res_partial_wo_firstBGP(unionNode.unions.size());

	// Plan 0: no change
	// pair.first = size, pair.second = cost
	// Only actually compute this if not already computed before
	long long cost_bgp_ori;
	auto bgp_query_outer = make_shared<BGPQuery>(outerBGP.patterns);
	getEncodeVarset(encode_varset, useful, outerBGP.patterns);
	curr_exist = bgp_query_outer->EncodeSmallBGPQuery(bgp_query_total.get(), kvstore, encode_varset.vars, \
		this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);
	pair<long long, long long> est_outer;
	if (curr_exist)
		est_outer = optimizer_->get_est_for_BGP(bgp_query_outer);
	else
		est_outer = make_pair(0, 0);
	cost_bgp_ori = est_outer.second;
	vector<shared_ptr<BGPQuery>> bgp_query_inner;
	vector<pair<long long, long long> > est_inner;
	for (size_t i = 0; i < multiBGP.size(); i++)
	{
		bgp_query_inner.emplace_back(make_shared<BGPQuery>(multiBGP[i].patterns));
		
		getEncodeVarset(encode_varset, useful, multiBGP[i].patterns);
		curr_exist = (bgp_query_inner.back())->EncodeSmallBGPQuery(bgp_query_total.get(), kvstore, encode_varset.vars, \
			this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);
		
		if (curr_exist)
			est_inner.emplace_back(optimizer_->get_est_for_BGP(bgp_query_inner.back()));
		else
			est_inner.emplace_back(make_pair(0, 0));
		cost_bgp_ori += est_inner.back().second;
	}
	long long cost_alg_ori = 0, res_union_ori = 0;
	for (size_t i = 0; i < unionNode.unions.size(); i++)
	{
		res_partial = 1;
		bool firstBGP = true;
		for (size_t j = 0; j < unionNode.unions[i].sub_group_pattern.size(); j++)
		{
			if (unionNode.unions[i].sub_group_pattern[j].type == \
				QueryTree::GroupPattern::SubGroupPattern::BGP_type)
			{
				if (firstBGP)
				{
					firstBGP = false;
					res_partial *= est_inner[bgp_idx].first;
				}
				else
				{
					auto bgp_query_tmp = make_shared<BGPQuery>(unionNode.unions[i].sub_group_pattern[j].patterns);
					getEncodeVarset(encode_varset, useful, unionNode.unions[i].sub_group_pattern[j].patterns);
					curr_exist = bgp_query_tmp->EncodeSmallBGPQuery(bgp_query_total.get(), kvstore, encode_varset.vars, \
						this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);
					if (curr_exist)
						res_partial *= optimizer_->get_est_for_BGP(bgp_query_tmp).first;
					else
						res_partial = 0;
				}
			}
			else
				res_partial *= unionNode.unions[i].sub_group_pattern[j].resSz;	// TODO: now assume resSz is already set, need to make sure
		}

		// If no join happens, don't need to add join cost
		if (unionNode.unions[i].sub_group_pattern.size() > 1)
			cost_alg_ori += res_partial;
		res_union_ori += res_partial;
		if (firstBGP)
		{
			res_partial_wo_firstBGP[i] = res_partial;
			hasBGP[i] = false;
		}
		else
		{
			if (est_inner[bgp_idx].first != 0)
				res_partial_wo_firstBGP[i] = res_partial / est_inner[bgp_idx++].first;
			else
				res_partial_wo_firstBGP[i] = res_partial;
			hasBGP[i] = true;
		}
	}
	cost_alg_ori += res_union_ori;
	cost_alg_ori += est_outer.first * res_union_ori;
	// Actual formula: cost_alg_ori = (est_outer.first + 2) * res_union_ori; (if all join happens)
	cout << "Plan 0:" << endl;
	cout << "bgp_query_outer:" << endl;
	for (size_t i = 0; i < bgp_query_outer->get_triple_vt().size(); i++)
		cout << bgp_query_outer->get_triple_vt()[i].subject << " " \
			<< bgp_query_outer->get_triple_vt()[i].predicate << " " \
			<< bgp_query_outer->get_triple_vt()[i].object << endl;
	cout << "est_outer: " << est_outer.first << " " << est_outer.second << endl;
	for (size_t i = 0; i < bgp_query_inner.size(); i++)
	{
		cout << "bgp_query_inner[" << i << "]: " << endl;
		for (size_t j = 0; j < bgp_query_inner[i]->get_triple_vt().size(); j++)
		{
			cout << bgp_query_inner[i]->get_triple_vt()[j].subject << " " \
				<< bgp_query_inner[i]->get_triple_vt()[j].predicate << " " \
				<< bgp_query_inner[i]->get_triple_vt()[j].object << endl;
		}
		cout << "est_inner[" << i << "]: " << est_inner[i].first << " " << est_inner[i].second << endl;
	}
	cout << "cost_bgp_ori = " << cost_bgp_ori << ", cost_alg_ori = " << cost_alg_ori << endl;
	if (cost[0] == LLONG_MAX)
	{
		cost[0] = cost_bgp_ori + cost_alg_ori;
	}

	// Plan 1: A {B} UNION {C} = {AB} UNION {AC}
	// Handled disconnected case (cannot be merged into a single BGP)
	vector<bool> connectedBGP(multiBGP.size(), true);
	size_t numConnected = multiBGP.size();
	long long cost_bgp_plan1 = 0;
	vector<shared_ptr<BGPQuery>> bgp_query_plan1;
	vector<pair<long long, long long> > est_plan1;
	vector<QueryTree::GroupPattern::Pattern> tmp_patterns;
	for (size_t i = 0; i < multiBGP.size(); i++)
	{
		if (!isBGPconnected(outerBGP.patterns, multiBGP[i].patterns))
		{
			connectedBGP[i] = false;
			numConnected--;
			continue;
		}

		Util::vectorSum<QueryTree::GroupPattern::Pattern>(outerBGP.patterns, multiBGP[i].patterns, tmp_patterns);
		bgp_query_plan1.emplace_back(make_shared<BGPQuery>(tmp_patterns));

		getEncodeVarset(encode_varset, useful, tmp_patterns);
		curr_exist = (bgp_query_plan1.back())->EncodeSmallBGPQuery(bgp_query_total.get(), kvstore, encode_varset.vars, \
			this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);

		if (curr_exist)
			est_plan1.emplace_back(optimizer_->get_est_for_BGP(bgp_query_plan1.back()));
		else
			est_plan1.emplace_back(make_pair(0, 0));
		cost_bgp_plan1 += est_plan1.back().second;
	}
	// If some UNION'd graph pattern doesn't have a connected BGP, need to add back A cost
	// When A is pushed down to multiple UNION'd graph patterns without BGP, consider it
	// evaluated only once for now (the BGP result is cached)
	if (numConnected < unionNode.unions.size())
		cost_bgp_plan1 += est_outer.second;
	long long cost_alg_plan1 = 0, res_union_plan1 = 0;
	bgp_idx = 0;
	for (size_t i = 0; i < unionNode.unions.size(); i++)
	{
		if (hasBGP[i])
		{
			if (connectedBGP[bgp_idx])
				res_partial = res_partial_wo_firstBGP[i] * est_plan1[bgp_idx].first;
			else
				res_partial = res_partial_wo_firstBGP[i] * est_inner[bgp_idx].first * est_outer.first;
			bgp_idx++;
		}
		else
			res_partial = res_partial_wo_firstBGP[i] * est_outer.first;
		// If no join happens, don't need to add join cost
		if (unionNode.unions[i].sub_group_pattern.size() > 1 || !hasBGP[i] \
			|| (hasBGP[i] && !connectedBGP[bgp_idx] - 1))
			cost_alg_plan1 += res_partial;
		res_union_plan1 += res_partial;
	}
	cost_alg_plan1 += res_union_plan1;
	cout << "Plan 1:" << endl;
	for (size_t i = 0; i < bgp_query_plan1.size(); i++)
	{
		cout << "bgp_query_plan1[" << i << "]: " << endl;
		for (size_t j = 0; j < bgp_query_plan1[i]->get_triple_vt().size(); j++)
		{
			cout << bgp_query_plan1[i]->get_triple_vt()[j].subject << " " \
				<< bgp_query_plan1[i]->get_triple_vt()[j].predicate << " " \
				<< bgp_query_plan1[i]->get_triple_vt()[j].object << endl;
		}
		cout << "est_plan1[" << i << "]: " << est_plan1[i].first << " " << est_plan1[i].second << endl;
	}
	cout << "cost_bgp_plan1 = " << cost_bgp_plan1 << ", cost_alg_plan1 = " << cost_alg_plan1 << endl;
	cost[1] = cost_bgp_plan1 + cost_alg_plan1;

	// The following two plans depend on MQO
	if (subBGP.empty())
		return;

	// Plan 2: A {B} UNION {C} = AD {B\D} UNION {C\D}
	// TODO: subtracting D may cause original BGP to become disconnected
	long long cost_bgp_plan2;
	Util::vectorSum<QueryTree::GroupPattern::Pattern>(outerBGP.patterns, subBGP, tmp_patterns);
	auto bgp_query_outer_plan2 = make_shared<BGPQuery>(tmp_patterns);
	getEncodeVarset(encode_varset, useful, tmp_patterns);
	curr_exist = bgp_query_outer_plan2->EncodeSmallBGPQuery(bgp_query_total.get(), kvstore, encode_varset.vars, \
		this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);
	pair<long long, long long> est_outer_plan2;
	if (curr_exist)
		est_outer_plan2 = optimizer_->get_est_for_BGP(bgp_query_outer_plan2);
	else
		est_outer_plan2 = make_pair(0, 0);
	cost_bgp_plan2 = est_outer_plan2.second;
	vector<shared_ptr<BGPQuery>> bgp_query_plan2;
	vector<pair<long long, long long> > est_plan2;
	for (size_t i = 0; i < multiBGP.size(); i++)
	{
		Util::vectorSubtract<QueryTree::GroupPattern::Pattern>(multiBGP[i].patterns, subBGP, tmp_patterns);
		bgp_query_plan2.emplace_back(make_shared<BGPQuery>(tmp_patterns));

		getEncodeVarset(encode_varset, useful, tmp_patterns);
		curr_exist = (bgp_query_plan2.back())->EncodeSmallBGPQuery(bgp_query_total.get(), kvstore, encode_varset.vars, \
			this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);

		if (curr_exist)
			est_plan2.emplace_back(optimizer_->get_est_for_BGP(bgp_query_plan2.back()));
		else
			est_plan2.emplace_back(make_pair(0, 0));
		cost_bgp_plan2 += est_plan2.back().second;
	}
	long long cost_alg_plan2 = 0, res_union_plan2 = 0;
	// Already ensured every UNION'd graph pattern has BGP
	for (size_t i = 0; i < unionNode.unions.size(); i++)
	{
		res_partial = res_partial_wo_firstBGP[i] * est_plan2[i].first;
		// If no join happens, don't need to add join cost
		if (unionNode.unions[i].sub_group_pattern.size() > 1)
			cost_alg_plan2 += res_partial;
		res_union_plan2 += res_partial;
	}
	cost_alg_plan2 += res_union_plan2;
	cost_alg_plan2 += est_outer_plan2.first * res_union_plan2;
	cost[2] = cost_bgp_plan2 + cost_alg_plan2;

	// Plan 3: A {B} UNION {C} = D {AB\D} UNION {AC\D}
	connectedBGP.assign(multiBGP.size(), true);
	numConnected = multiBGP.size();
	long long cost_bgp_plan3;
	auto bgp_query_outer_plan3 = make_shared<BGPQuery>(subBGP);
	getEncodeVarset(encode_varset, useful, subBGP);
	curr_exist = bgp_query_outer_plan3->EncodeSmallBGPQuery(bgp_query_total.get(), kvstore, encode_varset.vars, \
		this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);
	pair<long long, long long> est_outer_plan3;
	if (curr_exist)
		est_outer_plan3 = optimizer_->get_est_for_BGP(bgp_query_outer_plan3);
	else
		est_outer_plan3 = make_pair(0, 0);
	cost_bgp_plan3 = est_outer_plan3.second;
	vector<shared_ptr<BGPQuery>> bgp_query_plan3;
	vector<pair<long long, long long> > est_plan3;
	vector<QueryTree::GroupPattern::Pattern> tmp_patterns1;
	for (size_t i = 0; i < multiBGP.size(); i++)
	{
		Util::vectorSubtract<QueryTree::GroupPattern::Pattern>(multiBGP[i].patterns, subBGP, tmp_patterns1);
		Util::vectorSum<QueryTree::GroupPattern::Pattern>(outerBGP.patterns, tmp_patterns1, tmp_patterns);
		bgp_query_plan3.emplace_back(make_shared<BGPQuery>(tmp_patterns));

		if (!isBGPconnected(outerBGP.patterns, tmp_patterns))
		{
			connectedBGP[i] = false;
			numConnected--;
			continue;
		}

		getEncodeVarset(encode_varset, useful, tmp_patterns);
		curr_exist = (bgp_query_plan3.back())->EncodeSmallBGPQuery(bgp_query_total.get(), kvstore, encode_varset.vars, \
			this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);

		if (curr_exist)
			est_plan3.emplace_back(optimizer_->get_est_for_BGP(bgp_query_plan3.back()));
		else
			est_plan3.emplace_back(make_pair(0, 0));
		cost_bgp_plan3 += est_plan3.back().second;
	}
	long long cost_alg_plan3 = 0, res_union_plan3 = 0;
	// Already ensured every UNION'd graph pattern has BGP, but not necessarily connected
	for (size_t i = 0; i < unionNode.unions.size(); i++)
	{
		if (connectedBGP[i])
			res_partial = res_partial_wo_firstBGP[i] * est_plan3[i].first;
		else
			res_partial = res_partial_wo_firstBGP[i] * est_plan2[i].first * est_outer.first;
		// If no join happens, don't need to add join cost
		if (unionNode.unions[i].sub_group_pattern.size() > 1 || !connectedBGP[i])
			cost_alg_plan3 += res_partial;
		res_union_plan3 += res_partial;
	}
	cost_alg_plan3 += res_union_plan3;
	cost_alg_plan3 += est_outer_plan3.first * res_union_plan3;
	cost[3] = cost_bgp_plan3 + cost_alg_plan3;

	// TODO: pass out possible UNION resSz (4 plans, 4 possibilities)
	
	return;
}

void GeneralEvaluation::extractLargestCommonSubBGP(const vector<QueryTree::GroupPattern::SubGroupPattern> &multiBGP, \
			vector<QueryTree::GroupPattern::Pattern> &subBGP)
{
	// Intersect from start to end
	// Initialize to be the first BGP
	// NOTE: blockid might be shuffled around, but doesn't matter
	if (multiBGP.empty())
		return;

	for (size_t i = 0; i < multiBGP[0].patterns.size(); i++)
		subBGP.emplace_back(multiBGP[0].patterns[i]);
	for (size_t i = 1; i < multiBGP.size(); i++)
	{
		size_t j = 0;
		while (j != subBGP.size())
		{
			if (find(multiBGP[i].patterns.begin(), multiBGP[i].patterns.end(), subBGP[j]) \
				== multiBGP[i].patterns.end())
				subBGP.erase(subBGP.begin() + j);
			else
				j++;
		}
		if (subBGP.empty())
			break;
	}

	return;
}

void GeneralEvaluation::getEncodeVarset(Varset &encode_varset, const Varset &useful, \
	const vector<QueryTree::GroupPattern::Pattern> &smallBGP)
{
	encode_varset = Varset();
	for (size_t i = 0; i < smallBGP.size(); i++)
		encode_varset += smallBGP[i].varset;
	if (!this->query_tree.checkProjectionAsterisk() && encode_varset.hasCommonVar(useful))
		encode_varset = encode_varset * useful;	// Only the common vars remain
}

bool GeneralEvaluation::isBGPconnected(const vector<QueryTree::GroupPattern::Pattern> &patterns1, \
	const vector<QueryTree::GroupPattern::Pattern> &patterns2)
{
	for (size_t i = 0; i < patterns1.size(); i++)
	{
		if (patterns1[i].subject_object_varset.empty())
		{
			cout << "patterns1[i].subject_object_varset.empty" << endl;
			exit(0);
		}

		for (size_t j = 0; j < patterns2.size(); j++)
		{
			if (patterns2[j].subject_object_varset.empty())
			{
				cout << "patterns2[j].subject_object_varset.empty" << endl;
				exit(0);
			}

			if (patterns1[i].subject_object_varset.hasCommonVar(\
				patterns2[j].subject_object_varset))
				return true;
		}
	}
	return false;
}

QueryTree& GeneralEvaluation::getQueryTree()
{
	return this->query_tree;
}

bool GeneralEvaluation::doQuery()
{
	// query_tree.print();

	if (!this->query_tree.checkProjectionAsterisk() && this->query_tree.getProjection().empty())
		return false;

	this->query_tree.getGroupPattern().getVarset();

	this->query_tree.getGroupByVarset() = this->query_tree.getGroupByVarset() * this->query_tree.getGroupPattern().group_pattern_resultset_maximal_varset;

	if (this->query_tree.checkProjectionAsterisk() && this->query_tree.getProjection().empty() && !this->query_tree.getGroupByVarset().empty())
	{
		printf("[ERROR]	Select * and Group By can't appear at the same time.\n");
		return false;
	}

	if (!this->query_tree.checkSelectAggregateFunctionGroupByValid())
	{
		printf("[ERROR]	The vars/aggregate functions in the Select Clause are invalid.\n");
		return false;
	}

	if (this->query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset.hasCommonVar(this->query_tree.getGroupPattern().group_pattern_predicate_maximal_varset))
	{
		printf("[ERROR]	There are some vars occur both in subject/object and predicate.\n");
		return false;
	}

	// Gather and encode all triples
	addAllTriples(this->query_tree.getGroupPattern());
	bgp_query_total->EncodeBGPQuery(kvstore, vector<string>(), \
			this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);

	
	long trw_begin = Util::get_cur_time();
	rewriteQuery();
	long trw_end = Util::get_cur_time();
	printf("during Rewrite, used %ld ms.\n", trw_end - trw_begin);

	// exit(0);

	// this->strategy = Strategy(this->kvstore, this->pre2num,this->pre2sub, this->pre2obj, 
	// 	this->limitID_predicate, this->limitID_literal, this->limitID_entity,
	// 	this->query_tree.Modifier_Distinct== QueryTree::Modifier_Distinct, txn);

    // this->optimizer_ = make_shared<Optimizer>(kvstore,statistics,pre2num,pre2sub,pre2obj,triples_num,limitID_predicate,
    //                                   limitID_literal,limitID_entity,txn);

	// if (this->query_tree.checkWellDesigned())
	// if (false)
	// {
	// 	printf("=================\n");
	// 	printf("||well-designed||\n");
	// 	printf("=================\n");

	// 	this->rewriting_evaluation_stack.clear();
	// 	this->rewriting_evaluation_stack.push_back(EvaluationStackStruct());
	// 	this->rewriting_evaluation_stack.back().group_pattern = this->query_tree.getGroupPattern();
	// 	this->rewriting_evaluation_stack.back().result = NULL;

	// 	this->temp_result = this->rewritingBasedQueryEvaluation(0);
	// }
	// else
	// {
	// 	printf("=====================\n");
	// 	printf("||not well-designed||\n");
	// 	printf("=====================\n");

	// 	this->temp_result = this->semanticBasedQueryEvaluation(this->query_tree.getGroupPattern());
	// }

	this->rewriting_evaluation_stack.clear();
	this->rewriting_evaluation_stack.push_back(EvaluationStackStruct());
	this->rewriting_evaluation_stack.back().group_pattern = this->query_tree.getGroupPattern();
	this->rewriting_evaluation_stack.back().result = NULL;

	// this->temp_result = this->queryEvaluation(0);
	this->temp_result = this->queryEvaluationAfterOpt(0);

	return true;
}

void GeneralEvaluation::getAllPattern(const QueryTree::GroupPattern &group_pattern, vector<QueryTree::GroupPattern::Pattern> &vp)
{
	for (int i = 0; i < (int)group_pattern.sub_group_pattern.size(); i++)
	{
		if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Group_type)
			getAllPattern(group_pattern.sub_group_pattern[i].group_pattern, vp);
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
			vp.push_back(group_pattern.sub_group_pattern[i].pattern);
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Union_type)
		{
			for (int j = 0; j < (int)group_pattern.sub_group_pattern[i].unions.size(); j++)
				getAllPattern(group_pattern.sub_group_pattern[i].unions[j], vp);
		}
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Optional_type \
			|| group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Minus_type)
			getAllPattern(group_pattern.sub_group_pattern[i].optional, vp);
	}
}

TempResultSet* GeneralEvaluation::queryEvaluationAfterOpt(int dep)
{
	// If ASK query, and only one BGP, check if every triple consists of all constants
	// If so, set a special result so that getFinalResult will know
	if (this->query_tree.getQueryForm() == QueryTree::Ask_Query)
	{
		bool singleBGP = true, allConstants = true;
		vector<Triple> triple_vt;
		for (int i = 0; i < this->rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern.size(); i++)
		{
			if (this->rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[i].type \
				!= QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
			{
				singleBGP = false;
				break;
			}
			// Check if the triple consists of all constants
			if (this->rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[i].pattern.subject.value[0] == '?' \
				|| this->rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[i].pattern.predicate.value[0] == '?' \
				|| this->rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[i].pattern.object.value[0] == '?')
			{
				allConstants = false;
				break;
			}
			triple_vt.push_back(Triple(this->rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[i].pattern.subject.value, \
				this->rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[i].pattern.predicate.value, \
				this->rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[i].pattern.object.value));
		}
		if (singleBGP && allConstants)
		{
			// Check if these constant triples exist in database
			bool exist = BGPQuery::CheckConstBGPExist(triple_vt, kvstore);
			// Set a special result (indicates true/false) 
			TempResultSet *result = new TempResultSet();
			(*result).results.push_back(TempResult());
			(*result).results[0].result.push_back(TempResult::ResultPair());
			if (exist)
				(*result).results[0].result[0].str.push_back("true");
			else
				(*result).results[0].result[0].str.push_back("false");
			return result;
		}
	}

	QueryTree::GroupPattern group_pattern = rewriting_evaluation_stack[dep].group_pattern;
	TempResultSet *result = new TempResultSet();

	// Iterate across all sub-group-patterns, process according to type
	for (int i = 0; i < (int)group_pattern.sub_group_pattern.size(); i++)
		if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Group_type)
		{
			group_pattern.sub_group_pattern[i].group_pattern.print(0);
			this->rewriting_evaluation_stack.push_back(EvaluationStackStruct());
			this->rewriting_evaluation_stack.back().group_pattern = group_pattern.sub_group_pattern[i].group_pattern;
			this->rewriting_evaluation_stack.back().result = NULL;
			TempResultSet *temp = queryEvaluationAfterOpt(dep + 1);

			if (result->results.empty())
			{
				delete result;
				result = temp;
			}
			else
			{
				TempResultSet *new_result = new TempResultSet();
				result->doJoin(*temp, *new_result, this->stringindex, this->query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset);

				temp->release();
				result->release();
				delete temp;
				delete result;

				result = new_result;
				result->initial = false;
			}
		}
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::BGP_type)
		{
			Varset occur;
			for (size_t j = 0; j < group_pattern.sub_group_pattern[i].patterns.size(); j++)
				occur += group_pattern.sub_group_pattern[i].patterns[j].varset;
			// Only execute when not all constant and check cache fails
			if (!occur.empty() && !checkBasicQueryCache(group_pattern.sub_group_pattern[i].patterns, result, occur))
			{
				auto bgp_query = make_shared<BGPQuery>();
				for (size_t j = 0; j < group_pattern.sub_group_pattern[i].patterns.size(); j++)
					bgp_query->AddTriple(Triple(\
						group_pattern.sub_group_pattern[i].patterns[j].subject.value,
						group_pattern.sub_group_pattern[i].patterns[j].predicate.value,
						group_pattern.sub_group_pattern[i].patterns[j].object.value));

				// Encode
				long tv_begin = Util::get_cur_time();
				bool curr_exist = bgp_query->EncodeSmallBGPQuery(bgp_query_total.get(), kvstore, occur.vars, \
					this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct);
				long tv_encode = Util::get_cur_time();
				printf("during Encode, used %ld ms.\n", tv_encode - tv_begin);

				// Execute (if all constants exist)
				if (curr_exist)
				{
					// Set candidate lists of common vars with the parent layer in rewriting_evaluation_stack //
					if (dep > 0)
					{
						int fill_dep = fillCandList(bgp_query, dep, occur.vars);
						if (fill_dep >= 0)
						{
							long tv_fillcand = Util::get_cur_time();
							printf("after FillCand, used %ld ms, fill_dep = %d.\n", tv_fillcand - tv_encode, fill_dep);
						}
						else
							printf("No stored result for FillCand\n");
					}
					
					QueryInfo query_info;

					query_info.limit_ = false;
					if(this->query_tree.getLimit()!=-1) {
						query_info.limit_ = true;
						query_info.limit_num_ = this->query_tree.getLimit();
					}

					query_info.is_distinct_ = this->query_tree.getProjectionModifier() == QueryTree::ProjectionModifier::Modifier_Distinct;

					query_info.ordered_by_expressions_ = make_shared<vector<QueryTree::Order>>();
					for(auto order_item:this->query_tree.getOrderVarVector())
						query_info.ordered_by_expressions_->push_back(order_item);

					unique_ptr<unsigned[]>& p2id = bgp_query->resultPositionToId();
					p2id = unique_ptr<unsigned[]>(new unsigned [occur.vars.size()]);
					for (size_t j = 0; j < occur.vars.size(); j++)
						p2id[j] = bgp_query->get_var_id_by_name(occur.vars[j]);
					bgp_query->print(kvstore);
					this->optimizer_->DoQuery(bgp_query,query_info);

				}
				long tv_handle = Util::get_cur_time();
				printf("during Handle, used %ld ms.\n", tv_handle - tv_encode);

				// Merge with current result
				TempResultSet *temp = new TempResultSet();
				temp->results.push_back(TempResult());
				temp->results[0].id_varset = occur;
				size_t varnum = occur.vars.size();
				vector<unsigned*> &basicquery_result = *(bgp_query->get_result_list_pointer());
				size_t basicquery_result_num = basicquery_result.size();
				temp->results[0].result.reserve(basicquery_result_num);
				for (int j = 0; j < basicquery_result_num; j++)
				{
					unsigned *v = new unsigned[varnum];
					memcpy(v, basicquery_result[j], sizeof(int) * varnum);
					temp->results[0].result.push_back(TempResult::ResultPair());
					temp->results[0].result.back().id = v;
					temp->results[0].result.back().sz = varnum;
				}
				if (this->query_cache != NULL)
				{
					//if unconnected, time is incorrect
					int time = tv_handle - tv_begin;
					long tv_bftry = Util::get_cur_time();
					bool success = this->query_cache->tryCaching(group_pattern.sub_group_pattern[i].patterns, \
						temp->results[0], time);
					if (success)	printf("QueryCache cached\n");
					else			printf("QueryCache didn't cache\n");
					long tv_aftry = Util::get_cur_time();
					printf("during tryCache, used %ld ms.\n", tv_aftry - tv_bftry);
				}
				if (result->results.empty())
				{
					delete result;
					result = temp;
				}
				else
				{
					TempResultSet *new_result = new TempResultSet();
					result->doJoin(*temp, *new_result, this->stringindex, \
						this->query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset);

					temp->release();
					result->release();
					delete temp;
					delete result;

					result = new_result;
				}
			}
			else if (occur.empty())
			{
				// All constant triple always has empty binding, join always empty
				delete result;
				result = new TempResultSet();
			}
		}
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Union_type)
		{
			TempResultSet *sub_result_outer = new TempResultSet();

			for (int j = 0; j < (int)group_pattern.sub_group_pattern[i].unions.size(); j++)
			{
				TempResultSet *sub_result;
				this->rewriting_evaluation_stack.push_back(EvaluationStackStruct());
				this->rewriting_evaluation_stack.back().group_pattern = group_pattern.sub_group_pattern[i].unions[j];
				this->rewriting_evaluation_stack.back().result = NULL;
				sub_result = queryEvaluationAfterOpt(dep + 1);
				if (sub_result_outer->results.empty())
				{
					delete sub_result_outer;
					sub_result_outer = sub_result;
				}
				else
				{
					TempResultSet *new_result = new TempResultSet();
					sub_result_outer->doUnion(*sub_result, *new_result);

					sub_result->release();
					sub_result_outer->release();
					delete sub_result;
					delete sub_result_outer;

					sub_result_outer = new_result;
				}
			}

			if (result->results.empty())
			{
				delete result;
				result = sub_result_outer;
			}
			else
			{
				TempResultSet *new_result = new TempResultSet();
				result->doJoin(*sub_result_outer, *new_result, this->stringindex, this->query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset);

				sub_result_outer->release();
				result->release();
				delete sub_result_outer;
				delete result;

				result = new_result;
			}
		}
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Optional_type || group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Minus_type)
		{
			// If the current result is already empty, don't need to go further
			if (result == NULL || result->results.size() == 0 \
				|| (result->results.size() == 1 && result->results[0].result.empty()))
				continue;

			this->rewriting_evaluation_stack.push_back(EvaluationStackStruct());
			this->rewriting_evaluation_stack.back().group_pattern = group_pattern.sub_group_pattern[i].optional;
			this->rewriting_evaluation_stack.back().result = NULL;
			this->rewriting_evaluation_stack[dep].result = result;	// For OPTIONAL fillCandList
			TempResultSet *temp = queryEvaluationAfterOpt(dep + 1);

			TempResultSet *new_result = new TempResultSet();

			if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Optional_type)
				result->doOptional(*temp, *new_result, this->stringindex, this->query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset);
			else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Minus_type)
				result->doMinus(*temp, *new_result, this->stringindex, this->query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset);

			temp->release();
			result->release();
			delete temp;
			delete result;

			result = new_result;
			result->initial = false;
		}
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Filter_type)
		{
			TempResultSet *new_result = new TempResultSet();
			result->doFilter(group_pattern.sub_group_pattern[i].filter, *new_result, this->stringindex, group_pattern.group_pattern_subject_object_maximal_varset);

			result->release();
			delete result;

			result = new_result;
			result->initial = false;
		}
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Bind_type)
		{
			TempResultSet *temp = new TempResultSet();
			temp->results.push_back(TempResult());

			temp->results[0].str_varset = group_pattern.sub_group_pattern[i].bind.varset;

			temp->results[0].result.push_back(TempResult::ResultPair());
			temp->results[0].result[0].str.push_back(group_pattern.sub_group_pattern[i].bind.str);

			{
				TempResultSet *new_result = new TempResultSet();
				result->doJoin(*temp, *new_result, this->stringindex, this->query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset);

				temp->release();
				result->release();
				delete temp;
				delete result;

				result = new_result;
				result->initial = false;
			}
		}

	// result->print();

	this->rewriting_evaluation_stack.pop_back();

	return result;
}

bool GeneralEvaluation::expanseFirstOuterUnionGroupPattern(QueryTree::GroupPattern &group_pattern, deque<QueryTree::GroupPattern> &queue)
{
	// group_pattern is the top element of queue
	int first_union_pos = -1;

	// Iterate across sub-group-patterns, find the first UNION
	for (int i = 0; i < (int)group_pattern.sub_group_pattern.size(); i++)
		if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Union_type)
		{
			first_union_pos = i;
			break;
		}

	// None of the sub-group-patterns is UNION (expansion complete), exit with false
	if (first_union_pos == -1)
		return false;

	// Extract the UNION'ed sub-group-patterns, raise them one-by-one to the level of the original UNION;
	// push each newly constructed group-pattern into queue.
	// For example, if the input group-pattern is A (B UNION C) (D UNION E), then the queue has two items:
	// A B (D UNION E), A C (D UNION E)
	// Each item can still be expanded once by consecutive calls to this function (expanseFirstOuterUnionGroupPattern)
	for (int i = 0; i < (int)group_pattern.sub_group_pattern[first_union_pos].unions.size(); i++)
	{
		QueryTree::GroupPattern new_group_pattern;

		for (int j = 0; j < first_union_pos; j++)
			new_group_pattern.sub_group_pattern.push_back(group_pattern.sub_group_pattern[j]);

		for (int j = 0; j < (int)group_pattern.sub_group_pattern[first_union_pos].unions[i].sub_group_pattern.size(); j++)
			new_group_pattern.sub_group_pattern.push_back(group_pattern.sub_group_pattern[first_union_pos].unions[i].sub_group_pattern[j]);

		for (int j = first_union_pos + 1; j < (int)group_pattern.sub_group_pattern.size(); j++)
			new_group_pattern.sub_group_pattern.push_back(group_pattern.sub_group_pattern[j]);

		queue.push_back(new_group_pattern);
	}

	return true;
}

void GeneralEvaluation::getFinalResult(ResultSet &ret_result)
{

	if (this->temp_result == NULL)
	{
		return;
	}

	if (this->query_tree.getQueryForm() == QueryTree::Select_Query)
	{
		long t0 = Util::get_cur_time();

		if (this->temp_result->results.empty())
		{
			this->temp_result->results.push_back(TempResult());
			this->temp_result->results[0].id_varset += this->query_tree.getGroupPattern().group_pattern_resultset_maximal_varset;
		}

		Varset useful = this->query_tree.getResultProjectionVarset() + this->query_tree.getGroupByVarset() \
						+ this->query_tree.getOrderByVarset();

		if (this->query_tree.checkProjectionAsterisk())
		{
			for (int i = 0; i < (int)this->temp_result->results.size(); i++)
				useful += this->temp_result->results[i].getAllVarset();
		}

		if ((int)this->temp_result->results.size() > 1 || this->query_tree.getProjectionModifier() == QueryTree::Modifier_Distinct)
		{
			TempResultSet *new_temp_result = new TempResultSet();

			this->temp_result->doProjection1(useful, *new_temp_result, this->stringindex, this->query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset);

			this->temp_result->release();
			delete this->temp_result;

			this->temp_result = new_temp_result;
		}

		if (this->query_tree.checkAtLeastOneAggregateFunction() || !this->query_tree.getGroupByVarset().empty())
		{
			// vector<QueryTree::ProjectionVar> &proj = this->query_tree.getProjection();
			vector<QueryTree::ProjectionVar> proj = this->query_tree.getProjection();
			vector<string> order_vars = query_tree.getOrderByVarset().vars;
			for (string var : order_vars)
			{
				QueryTree::ProjectionVar proj_var;
				proj_var.aggregate_type = QueryTree::ProjectionVar::None_type;
				proj_var.var = var;
				proj.push_back(proj_var);
			}

			TempResultSet *new_temp_result = new TempResultSet();
			new_temp_result->results.push_back(TempResult());

			TempResult &result0 = this->temp_result->results[0];
			TempResult &new_result0 = new_temp_result->results[0];

			for (int i = 0; i < (int)proj.size(); i++)
				if (proj[i].aggregate_type == QueryTree::ProjectionVar::None_type)
				{
					if (result0.id_varset.findVar(proj[i].var))
						new_result0.id_varset.addVar(proj[i].var);
					else if (result0.str_varset.findVar(proj[i].var))
						new_result0.str_varset.addVar(proj[i].var);
				}
				else
					new_result0.str_varset.addVar(proj[i].var);

			vector<int> proj2temp((int)proj.size(), -1);
			for (int i = 0; i < (int)proj.size(); i++)
				if (proj[i].aggregate_type == QueryTree::ProjectionVar::None_type)
					proj2temp[i] = Varset(proj[i].var).mapTo(result0.getAllVarset())[0];
				else if (proj[i].aggregate_var != "*")
					proj2temp[i] = Varset(proj[i].aggregate_var).mapTo(result0.getAllVarset())[0];

			vector<int> proj2new = this->query_tree.getProjectionVarset().mapTo(new_result0.getAllVarset());

			int result0_size = (int)result0.result.size();
			vector<int> group2temp;

			if (!this->query_tree.getGroupByVarset().empty())
			{
				group2temp = this->query_tree.getGroupByVarset().mapTo(result0.getAllVarset());
				result0.sort(0, result0_size - 1, group2temp);
			}

			TempResultSet *temp_result_distinct = NULL;
			vector<int> group2distinct;

			for (int i = 0; i < (int)proj.size(); i++)
				if (proj[i].aggregate_type == QueryTree::ProjectionVar::Count_type && proj[i].distinct && proj[i].aggregate_var == "*")
				{
					temp_result_distinct = new TempResultSet();

					this->temp_result->doDistinct1(*temp_result_distinct);
					group2distinct = this->query_tree.getGroupByVarset().mapTo(temp_result_distinct->results[0].getAllVarset());
					temp_result_distinct->results[0].sort(0, (int)temp_result_distinct->results[0].result.size() - 1, group2distinct);

					break;
				}

			int result0_id_cols = result0.id_varset.getVarsetSize();
			int new_result0_id_cols = new_result0.id_varset.getVarsetSize();
			int new_result0_str_cols = new_result0.str_varset.getVarsetSize();

			// TODO: (Refactor)
			// 1. Make a fine distinction between aggregates and non-aggregate functions
			// 2. For non-aggregate functions, split argument gathering and executing
			// (possibly get rid of PathArgs and stick with a string vector for args?)
			if (result0_size == 0 && proj.size() == 1 && \
				proj[0].aggregate_type != QueryTree::ProjectionVar::None_type && \
				proj[0].aggregate_type != QueryTree::ProjectionVar::CompTree_type && \
				proj[0].aggregate_type != QueryTree::ProjectionVar::Count_type && \
				proj[0].aggregate_type != QueryTree::ProjectionVar::Sum_type && \
				proj[0].aggregate_type != QueryTree::ProjectionVar::Avg_type && \
				proj[0].aggregate_type != QueryTree::ProjectionVar::Min_type && \
				proj[0].aggregate_type != QueryTree::ProjectionVar::Max_type && \
				proj[0].aggregate_type != QueryTree::ProjectionVar::Contains_type)
			{
				// Non-aggregate functions, no var (all constant) in arg list
				// TODO: handle CONTAINS and custom functions

				new_result0.result.push_back(TempResult::ResultPair());
				new_result0.result.back().id = new unsigned[new_result0_id_cols];
				new_result0.result.back().sz = new_result0_id_cols;
				new_result0.result.back().str.resize(new_result0_str_cols);

				for (int i = 0; i < new_result0_id_cols; i++)
					new_result0.result.back().id[i] = INVALID;

				// Path functions
				if (proj[0].aggregate_type != QueryTree::ProjectionVar::Custom_type)
				{
					prepPathQuery();
					vector<int> uid_ls, vid_ls;
					vector<int> pred_id_set;
					uid_ls.push_back(kvstore->getIDByString(proj[0].path_args.src));
					if (proj[0].aggregate_type != QueryTree::ProjectionVar::ppr_type)
						vid_ls.push_back(kvstore->getIDByString(proj[0].path_args.dst));
					else
						vid_ls.push_back(-1);	// Dummy for loop
					if (!proj[0].path_args.pred_set.empty())
					{
						for (auto pred : proj[0].path_args.pred_set)
							pred_id_set.push_back(kvstore->getIDByPredicate(pred));
					}
					else
					{
						// Allow all predicates
						unsigned pre_num = stringindex->getNum(StringIndexFile::Predicate);
						for (unsigned j = 0; j < pre_num; j++)
							pred_id_set.push_back(j);
					}

					// For each u-v pair, query
					bool exist = 0, earlyBreak = 0;	// Boolean queries can break early with true
					stringstream ss;
					bool notFirstOutput = 0;	// For outputting commas
					ss << "\"{\"paths\":[";
					cout<<"proj[0].aggregate_type :"<<proj[0].aggregate_type <<endl;
					for (int uid : uid_ls)
					{
						for (int vid : vid_ls)
						{
						
							if (proj[0].aggregate_type == QueryTree::ProjectionVar::cyclePath_type)
							{
								if (uid == vid)
									continue;
								vector<int> path = pqHandler->cycle(uid, vid, proj[0].path_args.directed, pred_id_set);
								if (path.size() != 0)
								{
									if (notFirstOutput)
										ss << ",";
									else
										notFirstOutput = 1;
									pathVec2JSON(uid, vid, path, ss);
								}
							}
							else if (proj[0].aggregate_type == QueryTree::ProjectionVar::cycleBoolean_type)
							{
								if (uid == vid)
									continue;
								if (pqHandler->cycle(uid, vid, proj[0].path_args.directed, pred_id_set).size() != 0)
								{
									exist = 1;
									earlyBreak = 1;
									break;
								}
							}
							else if (proj[0].aggregate_type == QueryTree::ProjectionVar::shortestPath_type)
							{
								if (uid == vid)
								{
									if (notFirstOutput)
										ss << ",";
									else
										notFirstOutput = 1;
									vector<int> path; // Empty path
									pathVec2JSON(uid, vid, path, ss);
									continue;
								}
								vector<int> path = pqHandler->shortestPath(uid, vid, proj[0].path_args.directed, pred_id_set);
								if (path.size() != 0)
								{
									if (notFirstOutput)
										ss << ",";
									else
										notFirstOutput = 1;
									pathVec2JSON(uid, vid, path, ss);
								}
							}
							else if (proj[0].aggregate_type == QueryTree::ProjectionVar::kHopReachablePath_type)
							{
								cout << "begin run  kHopReachablePath " << endl;
								if (uid == vid)
								{
									if (notFirstOutput)
										ss << ",";
									else
										notFirstOutput = 1;
									vector<int> path; // Empty path
									pathVec2JSON(uid, vid, path, ss);
									continue;
								}
								int hopConstraint = proj[0].path_args.k;
								if (hopConstraint < 0)
									hopConstraint = 999;
								vector<int> path = pqHandler->kHopReachablePath(uid, vid, proj[0].path_args.directed, hopConstraint, pred_id_set);
								if (path.size() != 0)
								{
									if (notFirstOutput)
										ss << ",";
									else
										notFirstOutput = 1;
									pathVec2JSON(uid, vid, path, ss);
								}
							}
							else if (proj[0].aggregate_type == QueryTree::ProjectionVar::shortestPathLen_type)
							{
								if (uid == vid)
								{
									if (notFirstOutput)
										ss << ",";
									else
										notFirstOutput = 1;
									ss << "{\"src\":\"" << kvstore->getStringByID(uid)
									   << "\",\"dst\":\"" << kvstore->getStringByID(vid)
									   << "\",\"length\":0}";
									continue;
								}
								vector<int> path = pqHandler->shortestPath(uid, vid, proj[0].path_args.directed, pred_id_set);
								if (path.size() != 0)
								{
									if (notFirstOutput)
										ss << ",";
									else
										notFirstOutput = 1;
									ss << "{\"src\":\"" << kvstore->getStringByID(uid)
									   << "\",\"dst\":\"" << kvstore->getStringByID(vid) << "\",\"length\":";
									ss << (path.size() - 1) / 2;
									ss << "}";
								}
							}
							else if (proj[0].aggregate_type == QueryTree::ProjectionVar::kHopReachable_type)
							{
								if (uid == vid)
								{
									if (notFirstOutput)
										ss << ",";
									else
										notFirstOutput = 1;
									ss << "{\"src\":\"" << kvstore->getStringByID(uid)
									   << "\",\"dst\":\"" << kvstore->getStringByID(vid)
									   << "\",\"value\":\"true\"}";
									continue;
								}
								int hopConstraint = proj[0].path_args.k;
								if (hopConstraint < 0)
									hopConstraint = 999;
								bool reachRes = pqHandler->kHopReachable(uid, vid, proj[0].path_args.directed, hopConstraint, pred_id_set);
								ss << "{\"src\":\"" << kvstore->getStringByID(uid) << "\",\"dst\":\""
								   << kvstore->getStringByID(vid) << "\",\"value\":";
								if (reachRes)
									ss << "\"true\"}";
								else
									ss << "\"false\"}";
								// cout << "src = " << kvstore->getStringByID(uid) << ", dst = " << kvstore->getStringByID(vid) << endl;
							}
							else if (proj[0].aggregate_type == QueryTree::ProjectionVar::ppr_type)
							{
								vector<pair<int, double>> v2ppr;
								pqHandler->SSPPR(uid, proj[0].path_args.retNum, proj[0].path_args.k, pred_id_set, v2ppr);
								ss << "{\"src\":\"" << kvstore->getStringByID(uid) << "\",\"results\":[";
								for (auto it = v2ppr.begin(); it != v2ppr.end(); ++it)
								{
									if (it != v2ppr.begin())
										ss << ",";
									ss << "{\"dst\":\"" << kvstore->getStringByID(it->first) << "\",\"PPR\":"
									   << it->second << "}";
								}
								ss << "]}";
							}
						}
						if (earlyBreak)
							break;
					}
					ss << "]}\"";
					if (proj[0].aggregate_type == QueryTree::ProjectionVar::cycleBoolean_type)
					{
						if (exist)
							new_result0.result.back().str[proj2new[0] - new_result0_id_cols] = "true";
						else
							new_result0.result.back().str[proj2new[0] - new_result0_id_cols] = "false";
					}
					else
						ss >> new_result0.result.back().str[proj2new[0] - new_result0_id_cols];
				}
				else
				{
					// Custom functions
					if (proj[0].aggregate_type == QueryTree::ProjectionVar::Custom_type \
						&& proj[0].custom_func_name == "PPR")
					{
						prepPathQuery();
						int uid = kvstore->getIDByString(proj[0].func_args[0]);
						int k = stoi(proj[0].func_args[1]);
						vector<int> pred_id_set;
						string pred;
						for (size_t i = 2; i < proj[0].func_args.size() - 1; i++)
						{
							pred = proj[0].func_args[i];
							query_parser.replacePrefix(pred);
							pred_id_set.push_back(kvstore->getIDByPredicate(pred));
						}
						if (pred_id_set.empty())
						{
							// Allow all predicates
							unsigned pre_num = stringindex->getNum(StringIndexFile::Predicate);
							for (unsigned j = 0; j < pre_num; j++)
								pred_id_set.push_back(j);
						}
						int retNum = stoi(proj[0].func_args[proj[0].func_args.size() - 1]);
						vector< pair<int ,double> > v2ppr;
						pqHandler->SSPPR(uid, retNum, k, pred_id_set, v2ppr);
						stringstream ss;
						ss << "\"{\"paths\":[{\"src\":\"" << proj[0].func_args[0] << "\",\"results\":[";
						for (auto it = v2ppr.begin(); it != v2ppr.end(); ++it)
						{
							if (it != v2ppr.begin())
								ss << ",";
							ss << "{\"dst\":\"" << kvstore->getStringByID(it->first) << "\",\"PPR\":" \
								<< it->second << "}";
						}
						ss << "]}]}\"";
						ss >> new_result0.result.back().str[proj2new[0] - new_result0_id_cols];
					}
				}
			}

			// Exclusive with the if branch above
			for (int begin = 0; begin < result0_size;)
			{
				// At the end of an iteration, begin will be set to end + 1
				// The value of end will depend on GROUP BY conditions
				int end;
				if (group2temp.empty())
					end = result0_size - 1;
				else
					end = result0.findRightBounder(group2temp, result0.result[begin], result0_id_cols, group2temp);

				new_result0.result.push_back(TempResult::ResultPair());
				new_result0.result.back().id = new unsigned[new_result0_id_cols];
				new_result0.result.back().sz = new_result0_id_cols;
				new_result0.result.back().str.resize(new_result0_str_cols);

				for (int i = 0; i < new_result0_id_cols; i++)
					new_result0.result.back().id[i] = INVALID;

				for (int i = 0; i < (int)proj.size(); i++)
				{
					if (proj[i].aggregate_type == QueryTree::ProjectionVar::None_type)
					{
						if (proj2temp[i] < result0_id_cols)
							new_result0.result.back().id[proj2new[i]] = result0.result[begin].id[proj2temp[i]];
						else
							new_result0.result.back().str[proj2new[i] - new_result0_id_cols] = result0.result[begin].str[proj2temp[i] - result0_id_cols];
					}
					else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Count_type
						|| proj[i].aggregate_type == QueryTree::ProjectionVar::Sum_type
						|| proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type
						|| proj[i].aggregate_type == QueryTree::ProjectionVar::Min_type
						|| proj[i].aggregate_type == QueryTree::ProjectionVar::Max_type)
					{
						int count = 0;
						bool numeric = false, datetime = false, succ = true;
						EvalMultitypeValue numeric_sum, numeric_min, numeric_max, datetime_min, datetime_max, tmp, res;
						numeric_sum.datatype = EvalMultitypeValue::xsd_integer;
						numeric_sum.int_value = 0;
						numeric_min.datatype = EvalMultitypeValue::xsd_integer;
						numeric_min.int_value = INT_MAX;
						numeric_max.datatype = EvalMultitypeValue::xsd_integer;
						numeric_max.int_value = INT_MIN;
						datetime_min.datatype = EvalMultitypeValue::xsd_datetime;
						datetime_max.datatype = EvalMultitypeValue::xsd_datetime;

						if (proj[i].aggregate_var != "*")
						{
							if (proj[i].distinct)
							{
								if (proj2temp[i] < result0_id_cols)
								{
									// set<int> count_set;
									set<string> count_set;
									for (int j = begin; j <= end; j++)
									{
										if (result0.result[j].id[proj2temp[i]] != INVALID)
										{
											bool isel = query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset.findVar(proj[i].aggregate_var);
											stringindex->randomAccess(result0.result[j].id[proj2temp[i]], &tmp.term_value, isel);
											tmp.deduceTypeValue();
											if (tmp.datatype != EvalMultitypeValue::xsd_integer
												&& tmp.datatype != EvalMultitypeValue::xsd_decimal
												&& tmp.datatype != EvalMultitypeValue::xsd_float
												&& tmp.datatype != EvalMultitypeValue::xsd_double)
											{
												if (proj[i].aggregate_type == QueryTree::ProjectionVar::Sum_type
													|| proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
												{
													cout << "[ERROR] Invalid type for SUM or AVG." << endl;
													continue;
												}
												else if ((proj[i].aggregate_type == QueryTree::ProjectionVar::Min_type
													|| proj[i].aggregate_type == QueryTree::ProjectionVar::Max_type)
													&& tmp.datatype != EvalMultitypeValue::xsd_datetime)
												{
													cout << "[ERROR] Invalid type for MIN or MAX." << endl;
													continue; 
												}
											}
											if (tmp.datatype == EvalMultitypeValue::xsd_datetime)
											{
												if (!datetime)
													datetime = true;
											}
											else
											{
												if (!numeric)
													numeric = true;
											}
											if (datetime && numeric && proj[i].aggregate_type != QueryTree::ProjectionVar::Count_type)
											{
												succ = false;
												break;
											}

											if (proj[i].aggregate_type == QueryTree::ProjectionVar::Count_type
												|| proj[i].aggregate_type == QueryTree::ProjectionVar::Sum_type
												|| proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
												count_set.insert(tmp.term_value);
											else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Min_type)
											{
												if (numeric)
												{
													res = numeric_min > tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														numeric_min = tmp;
												}
												else if (datetime)
												{
													res = datetime_min > tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														datetime_min = tmp;
												}
											}
											else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Max_type)
											{
												if (numeric)
												{
													res = numeric_max < tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														numeric_max = tmp;
												}
												else if (datetime)
												{
													res = datetime_max < tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														datetime_max = tmp;
												}
											}
										}
									}
									
									// Obtained count_set
									count = (int)count_set.size();
									if (proj[i].aggregate_type == QueryTree::ProjectionVar::Sum_type
										|| proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
									{
										for (string elem : count_set)
										{
											tmp.term_value = elem;
											tmp.deduceTypeValue();

											numeric_sum = numeric_sum + tmp;
										}
									}
									if (proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
									{
										tmp.term_value = "\"" + to_string(count) + "\"^^<http://www.w3.org/2001/XMLSchema#integer>";
										tmp.deduceTypeValue();

										numeric_sum = numeric_sum / tmp;
									}
								}
								else
								{
									set<string> count_set;
									for (int j = begin; j <= end; j++)
									{
										if (result0.result[j].str[proj2temp[i] - result0_id_cols].length() > 0)
										{
											tmp.term_value = result0.result[j].str[proj2temp[i] - result0_id_cols];
											tmp.deduceTypeValue();
											if (tmp.datatype != EvalMultitypeValue::xsd_integer
												&& tmp.datatype != EvalMultitypeValue::xsd_decimal
												&& tmp.datatype != EvalMultitypeValue::xsd_float
												&& tmp.datatype != EvalMultitypeValue::xsd_double)
											{
												if (proj[i].aggregate_type == QueryTree::ProjectionVar::Sum_type
													|| proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
												{
													cout << "[ERROR] Invalid type for SUM or AVG." << endl;
													continue;
												}
												else if ((proj[i].aggregate_type == QueryTree::ProjectionVar::Min_type
													|| proj[i].aggregate_type == QueryTree::ProjectionVar::Max_type)
													&& tmp.datatype != EvalMultitypeValue::xsd_datetime)
												{
													cout << "[ERROR] Invalid type for MIN or MAX." << endl;
													continue; 
												}
											}
											if (tmp.datatype == EvalMultitypeValue::xsd_datetime)
											{
												if (!datetime)
													datetime = true;
											}
											else
											{
												if (!numeric)
													numeric = true;
											}
											if (datetime && numeric && proj[i].aggregate_type != QueryTree::ProjectionVar::Count_type)
											{
												succ = false;
												break;
											}

											if (proj[i].aggregate_type == QueryTree::ProjectionVar::Count_type)
												count_set.insert(result0.result[j].str[proj2temp[i] - result0_id_cols]);
											else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Min_type)
											{
												if (numeric)
												{
													res = numeric_min > tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														numeric_min = tmp;
												}
												else if (datetime)
												{
													res = datetime_min > tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														datetime_min = tmp;
												}
											}
											else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Max_type)
											{
												if (numeric)
												{
													res = numeric_max < tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														numeric_max = tmp;
												}
												else if (datetime)
												{
													res = datetime_max < tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														datetime_max = tmp;
												}
											}

											count_set.insert(result0.result[j].str[proj2temp[i] - result0_id_cols]);
										}
									}

									// Obtained count_set
									count = (int)count_set.size();
									if (proj[i].aggregate_type == QueryTree::ProjectionVar::Sum_type
										|| proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
									{
										for (string elem : count_set)
										{
											tmp.term_value = elem;
											tmp.deduceTypeValue();

											numeric_sum = numeric_sum + tmp;
										}
									}
									if (proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
									{
										tmp.term_value = "\"" + to_string(count) + "\"^^<http://www.w3.org/2001/XMLSchema#integer>";
										tmp.deduceTypeValue();

										numeric_sum = numeric_sum / tmp;
									}
								}
							}
							else 	// No DISTINCT
							{
								if (proj2temp[i] < result0_id_cols)
								{
									for (int j = begin; j <= end; j++)
									{
										if (result0.result[j].id[proj2temp[i]] != INVALID)
										{
											bool isel = query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset.findVar(proj[i].aggregate_var);
											stringindex->randomAccess(result0.result[j].id[proj2temp[i]], &tmp.term_value, isel);
											tmp.deduceTypeValue();
											if (tmp.datatype != EvalMultitypeValue::xsd_integer
												&& tmp.datatype != EvalMultitypeValue::xsd_decimal
												&& tmp.datatype != EvalMultitypeValue::xsd_float
												&& tmp.datatype != EvalMultitypeValue::xsd_double)
											{
												if (proj[i].aggregate_type == QueryTree::ProjectionVar::Sum_type
													|| proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
												{
													cout << "[ERROR] Invalid type for SUM or AVG." << endl;
													continue;
												}
												else if ((proj[i].aggregate_type == QueryTree::ProjectionVar::Min_type
													|| proj[i].aggregate_type == QueryTree::ProjectionVar::Max_type)
													&& tmp.datatype != EvalMultitypeValue::xsd_datetime)
												{
													cout << "[ERROR] Invalid type for MIN or MAX." << endl;
													continue; 
												}
											}
											if (tmp.datatype == EvalMultitypeValue::xsd_datetime)
											{
												if (!datetime)
													datetime = true;
											}
											else
											{
												if (!numeric)
													numeric = true;
											}
											if (datetime && numeric && proj[i].aggregate_type != QueryTree::ProjectionVar::Count_type)
											{
												succ = false;
												break;
											}

											if (proj[i].aggregate_type == QueryTree::ProjectionVar::Sum_type
												|| proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
											{
												numeric_sum = numeric_sum + tmp;
												cout << "numeric_sum.term_value = " << numeric_sum.term_value << endl;
											}
											else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Min_type)
											{
												if (numeric)
												{
													res = numeric_min > tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														numeric_min = tmp;
												}
												else if (datetime)
												{
													res = datetime_min > tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														datetime_min = tmp;
												}
											}
											else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Max_type)
											{
												if (numeric)
												{
													res = numeric_max < tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														numeric_max = tmp;
												}
												else if (datetime)
												{
													res = datetime_max < tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														datetime_max = tmp;
												}
											}

											count++;
										}
									}
									if (proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
									{
										tmp.term_value = "\"" + to_string(count) + "\"^^<http://www.w3.org/2001/XMLSchema#integer>";
										tmp.deduceTypeValue();

										numeric_sum = numeric_sum / tmp;
									}
								}
								else
								{
									for (int j = begin; j <= end; j++)
									{
										if (result0.result[j].str[proj2temp[i] - result0_id_cols].length() > 0)
										{
											tmp.term_value = result0.result[j].str[proj2temp[i] - result0_id_cols];
											tmp.deduceTypeValue();
											if (tmp.datatype != EvalMultitypeValue::xsd_integer
												&& tmp.datatype != EvalMultitypeValue::xsd_decimal
												&& tmp.datatype != EvalMultitypeValue::xsd_float
												&& tmp.datatype != EvalMultitypeValue::xsd_double)
											{
												if (proj[i].aggregate_type == QueryTree::ProjectionVar::Sum_type
													|| proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
												{
													cout << "[ERROR] Invalid type for SUM or AVG." << endl;
													continue;
												}
												else if ((proj[i].aggregate_type == QueryTree::ProjectionVar::Min_type
													|| proj[i].aggregate_type == QueryTree::ProjectionVar::Max_type)
													&& tmp.datatype != EvalMultitypeValue::xsd_datetime)
												{
													cout << "[ERROR] Invalid type for MIN or MAX." << endl;
													continue; 
												}
											}
											if (tmp.datatype == EvalMultitypeValue::xsd_datetime)
											{
												if (!datetime)
													datetime = true;
											}
											else
											{
												if (!numeric)
													numeric = true;
											}
											if (datetime && numeric && proj[i].aggregate_type != QueryTree::ProjectionVar::Count_type)
											{
												succ = false;
												break;
											}

											if (proj[i].aggregate_type == QueryTree::ProjectionVar::Sum_type
												|| proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
											{
												numeric_sum = numeric_sum + tmp;
												cout << "numeric_sum.term_value = " << numeric_sum.term_value << endl;
											}
											else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Min_type)
											{
												if (numeric)
												{
													res = numeric_min > tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														numeric_min = tmp;
												}
												else if (datetime)
												{
													res = datetime_min > tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														datetime_min = tmp;
												}
											}
											else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Max_type)
											{
												if (numeric)
												{
													res = numeric_max < tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														numeric_max = tmp;
												}
												else if (datetime)
												{
													res = datetime_max < tmp;
													if (res.bool_value.value == EvalMultitypeValue::EffectiveBooleanValue::true_value)
														datetime_max = tmp;
												}
											}

											count++;
										}
									}

									if (proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
									{
										tmp.term_value = "\"" + to_string(count) + "\"^^<http://www.w3.org/2001/XMLSchema#integer>";
										tmp.deduceTypeValue();

										numeric_sum = numeric_sum / tmp;
									}
								}
							}
						}
						else 	// Only COUNT is possible
						{
							if (proj[i].distinct)
							{
								count = temp_result_distinct->results[0].findRightBounder(group2distinct, result0.result[begin], result0_id_cols, group2temp) -
									temp_result_distinct->results[0].findLeftBounder(group2distinct, result0.result[begin], result0_id_cols, group2temp) + 1;
							}
							else
							{
								count = end - begin + 1;
							}
						}

						// Write aggregate result
						stringstream ss;

						if (proj[i].aggregate_type == QueryTree::ProjectionVar::Count_type)
						{
							ss << "\"";
							ss << count;
							ss << "\"^^<http://www.w3.org/2001/XMLSchema#integer>";
						}
						else if (succ)
						{
							if (proj[i].aggregate_type == QueryTree::ProjectionVar::Sum_type
								|| proj[i].aggregate_type == QueryTree::ProjectionVar::Avg_type)
							{
								cout << "numeric_sum.term_value = " << numeric_sum.term_value << endl;
								ss << numeric_sum.term_value;
							}
							else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Min_type)
							{
								if (numeric)
									ss << numeric_min.term_value;
								else if (datetime)
									ss << datetime_min.term_value;
							}
							else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Max_type)
							{
								if (numeric)
									ss << numeric_max.term_value;
								else if (datetime)
									ss << datetime_max.term_value;
							}
							
						}
						else 	// Failed
							ss << "";

						ss >> new_result0.result.back().str[proj2new[i] - new_result0_id_cols];
					}
					else if (proj[i].aggregate_type == QueryTree::ProjectionVar::CompTree_type)
					{
						// Strictly speaking, not an aggregate; each original line will produce a line of results
						for (int j = begin; j <= end; j++)
						{
							new_result0.result.back().str[proj2new[i] - new_result0_id_cols] = \
								result0.doComp(proj[i].comp_tree_root, result0.result[j], result0_id_cols, stringindex, \
								query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset, \
								query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset).term_value;
							if (j < end)
							{
								new_result0.result.push_back(TempResult::ResultPair());
								new_result0.result.back().id = new unsigned[new_result0_id_cols];
								new_result0.result.back().sz = new_result0_id_cols;
								new_result0.result.back().str.resize(new_result0_str_cols);
							}
						}
					}
					else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Contains_type)
					{
						if (proj[i].func_args.size() == 2)
						{
							string arg1 = proj[i].func_args[0], arg2 = proj[i].func_args[1];
							int pos1 = -2, pos2 = -2;	// -2 for string, -1 for error var, others for correct var
														// But < result0_id_cols is impossible, because must be string
							bool isel1 = false, isel2 = false;
							string big_str = "", small_str = "";
							if (arg1[0] == '?')
							{
								pos1 = Varset(arg1).mapTo(query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset)[0];
								if (pos1 == -1)
								{
									cout << "[ERROR] Cannot get arg1 in CONTAINS." << endl;
									break;
								}
								isel1 = query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset.findVar(arg1);
							}
							else if (arg1[0] == '\"')
							{
								big_str = arg1;
								int idx = big_str.length() - 1;
								while (idx >= 0 && big_str[idx] != '"')
									idx--;
								if (idx >= 1)
									big_str = big_str.substr(1, idx - 1);
							}
							
							if (arg2[0] == '?')
							{
								pos2 = Varset(arg2).mapTo(query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset)[0];
								if (pos2 == -1)
								{
									cout << "[ERROR] Cannot get arg2 in CONTAINS." << endl;
									break;
								}
								isel2 = query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset.findVar(arg2);
							}
							else if (arg2[0] == '\"')
							{
								small_str = arg2;
								int idx = small_str.length() - 1;
								while (idx >= 0 && small_str[idx] != '"')
									idx--;
								if (idx >= 1)
									small_str = small_str.substr(1, idx - 1);
							}

							for (int j = begin; j <= end; j++)
							{
								if (pos1 >= 0)
								{
									if (pos1 < result0_id_cols)
										stringindex->randomAccess(result0.result[j].id[pos1], &big_str, isel1);
									else
										big_str = result0.result[j].str[pos1 - result0_id_cols];
									int idx = big_str.length() - 1;
									while (idx >= 0 && big_str[idx] != '"')
										idx--;
									if (idx >= 1)
										big_str = big_str.substr(1, idx - 1);
								}
								if (pos2 >= 0)
								{
									if (pos2 < result0_id_cols)
										stringindex->randomAccess(result0.result[j].id[pos2], &small_str, isel2);
									else
										small_str = result0.result[j].str[pos2 - result0_id_cols];
									int idx = small_str.length() - 1;
									while (idx >= 0 && small_str[idx] != '"')
										idx--;
									if (idx >= 1)
										small_str = small_str.substr(1, idx - 1);
								}

								cout << "big_str = " << big_str << endl;
								cout << "small_str = " << small_str << endl;

								if (big_str.find(small_str) != string::npos)
									new_result0.result.back().str[proj2new[i] - new_result0_id_cols] = \
										"\"true\"^^<http://www.w3.org/2001/XMLSchema#boolean>";
								else
									new_result0.result.back().str[proj2new[i] - new_result0_id_cols] = \
										"\"false\"^^<http://www.w3.org/2001/XMLSchema#boolean>";

								if (j < end)
								{
									new_result0.result.push_back(TempResult::ResultPair());
									new_result0.result.back().id = new unsigned[new_result0_id_cols];
									new_result0.result.back().sz = new_result0_id_cols;
									new_result0.result.back().str.resize(new_result0_str_cols);
								}
							}
						}
					}
					else if (proj[i].aggregate_type == QueryTree::ProjectionVar::Custom_type)
					{
						if (proj[i].custom_func_name == "PPR")
						{
							prepPathQuery();
							vector<int> uid_ls;
							int var2temp = Varset(proj[0].func_args[0]).mapTo(result0.getAllVarset())[0];
							if (var2temp >= result0_id_cols)
								cout << "[ERROR] src must be an entity!" << endl;	// TODO: throw exception
							else
							{
								for (int j = begin; j <= end; j++)
								{
									if (result0.result[j].id[var2temp] != INVALID)
										uid_ls.push_back(result0.result[j].id[var2temp]);
								}
							}
							int k = stoi(proj[0].func_args[1]);
							vector<int> pred_id_set;
							string pred;
							for (size_t i = 2; i < proj[0].func_args.size() - 1; i++)
							{
								pred = proj[0].func_args[i];
								query_parser.replacePrefix(pred);
								pred_id_set.push_back(kvstore->getIDByPredicate(pred));
							}
							if (pred_id_set.empty())
							{
								// Allow all predicates
								unsigned pre_num = stringindex->getNum(StringIndexFile::Predicate);
								for (unsigned j = 0; j < pre_num; j++)
									pred_id_set.push_back(j);
							}
							int retNum = stoi(proj[0].func_args[proj[0].func_args.size() - 1]);
							vector< pair<int ,double> > v2ppr;
							stringstream ss;
							ss << "\"{\"paths\":[";
							for (auto uid : uid_ls)
							{
								pqHandler->SSPPR(uid, retNum, k, pred_id_set, v2ppr);
								ss << "{\"src\":\"" << kvstore->getStringByID(uid) << "\",\"results\":[";
								for (auto it = v2ppr.begin(); it != v2ppr.end(); ++it)
								{
									if (it != v2ppr.begin())
										ss << ",";
									ss << "{\"dst\":\"" << kvstore->getStringByID(it->first) << "\",\"PPR\":" \
										<< it->second << "}";
								}
								ss << "]}";
							}
							ss << "]}\"";
							ss >> new_result0.result.back().str[proj2new[0] - new_result0_id_cols];
							string tmp;
							ss >> tmp;
							cout << "HERE !!!! " << tmp << endl;
						}
					}
					else	// Path query
					{
						prepPathQuery();

						vector<int> uid_ls, vid_ls;
						vector<int> pred_id_set;

						// uid
						if (proj[i].path_args.src[0] == '?')	// src is a variable
						{
							int var2temp = Varset(proj[i].path_args.src).mapTo(result0.getAllVarset())[0];
							if (var2temp >= result0_id_cols)
								cout << "[ERROR] src must be an entity!" << endl;	// TODO: throw exception
							else
							{
								for (int j = begin; j <= end; j++)
								{
									if (result0.result[j].id[var2temp] != INVALID)
										uid_ls.push_back(result0.result[j].id[var2temp]);
								}
							}
						}
						else	// src is an IRI
							uid_ls.push_back(kvstore->getIDByString(proj[i].path_args.src));

						// cout << "uid: ";
						// for (int uid : uid_ls)
						// 	cout << uid << ' ';
						// cout << endl;

						// vid
						if (proj[0].aggregate_type != QueryTree::ProjectionVar::ppr_type)
						{
							if (proj[i].path_args.dst[0] == '?')	// dst is a variable
							{
								int var2temp = Varset(proj[i].path_args.dst).mapTo(result0.getAllVarset())[0];
								cout << "vid var2temp = " << var2temp << endl;
								if (var2temp >= result0_id_cols)
									cout << "[ERROR] dst must be an entity!" << endl;	// TODO: throw exception
								else
								{
									for (int j = begin; j <= end; j++)
									{
										if (result0.result[j].id[var2temp] != INVALID)
											vid_ls.push_back(result0.result[j].id[var2temp]);
									}
								}
							}
							else	// dst is an IRI
								vid_ls.push_back(kvstore->getIDByString(proj[i].path_args.dst));

							// cout << "vid: ";
							// for (int vid : vid_ls)
							// 	cout << vid << ' ';
							// cout << endl;
						}
						else
							vid_ls.push_back(-1);	// Dummy for loop

						// pred_id_set: convert from IRI to integer ID
						if (!proj[i].path_args.pred_set.empty())
						{
							for (auto pred : proj[i].path_args.pred_set)
								pred_id_set.push_back(kvstore->getIDByPredicate(pred));
						}
						else
						{
							// Allow all predicates
							unsigned pre_num = stringindex->getNum(StringIndexFile::Predicate);
							for (unsigned j = 0; j < pre_num; j++)
								pred_id_set.push_back(j);
						}

						// For each u-v pair, query
						bool exist = 0, earlyBreak = 0;	// Boolean queries can break early with true
						stringstream ss;
						bool notFirstOutput = 0;	// For outputting commas
						ss << "\"{\"paths\":[";
						for (int uid : uid_ls)
						{
							for (int vid : vid_ls)
							{
								if (proj[i].aggregate_type == QueryTree::ProjectionVar::cyclePath_type)
								{
									if (uid == vid)
										continue;
									vector<int> path = pqHandler->cycle(uid, vid, proj[i].path_args.directed, pred_id_set);
									if (path.size() != 0)
									{
										if (notFirstOutput)
											ss << ",";
										else
											notFirstOutput = 1;
										pathVec2JSON(uid, vid, path, ss);
									}
								}
								else if (proj[i].aggregate_type == QueryTree::ProjectionVar::cycleBoolean_type)
								{
									if (uid == vid)
										continue;
									if (pqHandler->cycle(uid, vid, proj[i].path_args.directed, pred_id_set).size() != 0)
									{
										exist = 1;
										earlyBreak = 1;
										break;
									}
								}
								else if (proj[i].aggregate_type == QueryTree::ProjectionVar::shortestPath_type)
								{
									if (uid == vid)
									{
										if (notFirstOutput)
											ss << ",";
										else
											notFirstOutput = 1;
										vector<int> path;	// Empty path
										pathVec2JSON(uid, vid, path, ss);
										continue;
									}
									vector<int> path = pqHandler->shortestPath(uid, vid, proj[0].path_args.directed, pred_id_set);
									if (path.size() != 0)
									{
										if (notFirstOutput)
											ss << ",";
										else
											notFirstOutput = 1;
										pathVec2JSON(uid, vid, path, ss);
									}
								}
								else if (proj[i].aggregate_type == QueryTree::ProjectionVar::shortestPathLen_type)
								{
									if (uid == vid)
									{
										if (notFirstOutput)
											ss << ",";
										else
											notFirstOutput = 1;
										ss << "{\"src\":\"" << kvstore->getStringByID(uid) \
											<< "\",\"dst\":\"" << kvstore->getStringByID(vid) \
											<< "\",\"length\":0}";
										continue;
									}
									vector<int> path = pqHandler->shortestPath(uid, vid, proj[0].path_args.directed, pred_id_set);
									if (path.size() != 0)
									{
										if (notFirstOutput)
											ss << ",";
										else
											notFirstOutput = 1;
										ss << "{\"src\":\"" << kvstore->getStringByID(uid) \
											<< "\",\"dst\":\"" << kvstore->getStringByID(vid) << "\",\"length\":";
										ss << (path.size() - 1) / 2;
										ss << "}";
									}
								}
								else if (proj[i].aggregate_type == QueryTree::ProjectionVar::kHopReachable_type)
								{
									if (uid == vid)
									{
										if (notFirstOutput)
											ss << ",";
										else
											notFirstOutput = 1;
										ss << "{\"src\":\"" << kvstore->getStringByID(uid) \
											<< "\",\"dst\":\"" << kvstore->getStringByID(vid) \
											<< "\",\"value\":\"true\"}";
										continue;
									}
									int hopConstraint = proj[0].path_args.k;
									if (hopConstraint < 0)
										hopConstraint = 999;
									bool reachRes = pqHandler->kHopReachable(uid, vid, proj[0].path_args.directed, hopConstraint, pred_id_set);
									ss << "{\"src\":\"" << kvstore->getStringByID(uid) << "\",\"dst\":\"" \
										<< kvstore->getStringByID(vid) << "\",\"value\":";
									cout << "src = " << kvstore->getStringByID(uid) << ", dst = " << kvstore->getStringByID(vid) << endl;
									if (reachRes)
										ss << "\"true\"}";
									else
										ss << "\"false\"}";
								}
								else if (proj[0].aggregate_type == QueryTree::ProjectionVar::kHopReachablePath_type)
							{
								cout << "begin run  kHopReachablePath " << endl;
								if (uid == vid)
								{
									if (notFirstOutput)
										ss << ",";
									else
										notFirstOutput = 1;
									vector<int> path; // Empty path
									pathVec2JSON(uid, vid, path, ss);
									continue;
								}
								int hopConstraint = proj[0].path_args.k;
								if (hopConstraint < 0)
									hopConstraint = 999;
								vector<int> path = pqHandler->kHopReachablePath(uid, vid, proj[0].path_args.directed, hopConstraint, pred_id_set);
								if (path.size() != 0)
								{
									if (notFirstOutput)
										ss << ",";
									else
										notFirstOutput = 1;
									pathVec2JSON(uid, vid, path, ss);
								}
							}
								else if (proj[0].aggregate_type == QueryTree::ProjectionVar::ppr_type)
								{
									vector< pair<int ,double> > v2ppr;
									pqHandler->SSPPR(uid, proj[0].path_args.retNum, proj[0].path_args.k, pred_id_set, v2ppr);
									ss << "{\"src\":\"" << kvstore->getStringByID(uid) << "\",\"results\":[";
									for (auto it = v2ppr.begin(); it != v2ppr.end(); ++it)
									{
										if (it != v2ppr.begin())
											ss << ",";
										ss << "{\"dst\":\"" << kvstore->getStringByID(it->first) << "\",\"PPR\":" \
											<< it->second << "}";
									}
									ss << "]}";
								}
							}
							if (earlyBreak)
								break;
						}
						ss << "]}\"";
						if (proj[i].aggregate_type == QueryTree::ProjectionVar::cycleBoolean_type)
						{
							if (exist)
								new_result0.result.back().str[proj2new[i] - new_result0_id_cols] = "true";
							else
								new_result0.result.back().str[proj2new[i] - new_result0_id_cols] = "false";
						}
						else
							ss >> new_result0.result.back().str[proj2new[i] - new_result0_id_cols];
					}
				}
				begin = end + 1;
			}

			if (temp_result_distinct != NULL)
			{
				temp_result_distinct->release();
				delete temp_result_distinct;
			}

			this->temp_result->release();
			delete this->temp_result;

			this->temp_result = new_temp_result;
		}

		//temp_result --> ret_result

		if (this->query_tree.getProjectionModifier() == QueryTree::Modifier_Distinct)
		{
			// cout << "Modifier_Distinct!!!" << endl;

			TempResultSet *new_temp_result = new TempResultSet();

			this->temp_result->doDistinct1(*new_temp_result);

			this->temp_result->release();
			delete this->temp_result;

			this->temp_result = new_temp_result;
		}

		TempResult &result0 = this->temp_result->results[0];
		Varset ret_result_varset;

		if (this->query_tree.checkProjectionAsterisk() && this->query_tree.getProjectionVarset().empty())
		{
			ret_result.setVar(result0.getAllVarset().vars);
			ret_result_varset = result0.getAllVarset();
			ret_result.select_var_num = result0.getAllVarset().getVarsetSize();
			ret_result.true_select_var_num = ret_result.select_var_num;
		}
		else
		{
			vector<string> proj_vars = query_tree.getProjectionVarset().vars;
			vector<string> order_vars = query_tree.getOrderByVarset().vars;
			proj_vars.insert(proj_vars.end(), order_vars.begin(), order_vars.end());
			ret_result.setVar(proj_vars);
			ret_result_varset = query_tree.getProjectionVarset() + query_tree.getOrderByVarset();
			ret_result.select_var_num = ret_result_varset.getVarsetSize();
			ret_result.true_select_var_num = query_tree.getProjectionVarset().getVarsetSize();
		}

		ret_result.ansNum = (int)result0.result.size();
		ret_result.setOutputOffsetLimit(this->query_tree.getOffset(), this->query_tree.getLimit());

#ifdef STREAM_ON
		long long ret_result_size = (long long)ret_result.ansNum * (long long)ret_result.select_var_num * 100 / Util::GB;
		if (Util::memoryLeft() < ret_result_size || !this->query_tree.getOrderVarVector().empty())
		{
			ret_result.setUseStream();
			printf("set use Stream\n");
		}
#endif

		if (!ret_result.checkUseStream())
		{
			ret_result.answer = new string*[ret_result.ansNum];
			for (unsigned i = 0; i < ret_result.ansNum; i++)
				ret_result.answer[i] = NULL;
		}
		else
		{
			vector <unsigned> keys;
			vector <bool> desc;
			if (!(query_tree.getSingleBGP() && query_tree.getLimit() != -1))
			{
				// Else, ORDER BY will already have been processed at the BGP level
				for (int i = 0; i < (int)this->query_tree.getOrderVarVector().size(); i++)
				{
					// int var_id = Varset(this->query_tree.getOrderVarVector()[i].var).mapTo(ret_result_varset)[0];
					// Temporary, to be changed to allow for more than one var in one ORDER BY condition
					int var_id = this->query_tree.getOrderVarVector()[i].comp_tree_root.getVarset().mapTo(ret_result_varset)[0];
					if (var_id != -1)
					{
						keys.push_back(var_id);
						desc.push_back(this->query_tree.getOrderVarVector()[i].descending);
					}
				}
			}
			ret_result.openStream(keys, desc);
		}

		vector<int> proj2temp = ret_result_varset.mapTo(result0.getAllVarset());
		int id_cols = result0.id_varset.getVarsetSize();

		vector<bool> isel;	// Indicates whether the var is a subject/object (true) or predicate (false)
		for (int i = 0; i < result0.id_varset.getVarsetSize(); i++)
			isel.push_back(this->query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset.findVar(result0.id_varset.vars[i]));

		if (!ret_result.checkUseStream())
		{

			//pthread_t tidp;
			//long arg[6];
			vector<StringIndexFile* > a = this->stringindex->get_three_StringIndexFile();
			std::vector<StringIndexFile::AccessRequest> requestVectors[3];
			/*arg[0] = (long)&a;
			arg[1] = (long)&ret_result;
			arg[2] = (long)&proj2temp;
			arg[3] = (long)id_cols;
			arg[4] = (long)&result0;
			arg[5] = (long)&isel;
			pthread_create(&tidp, NULL, &preread_from_index, arg);*/

			unsigned retAnsNum = ret_result.ansNum;
			unsigned selectVar = ret_result.select_var_num;
			/*
			int counterEntity = 0;
			int counterLiteral = 0;
			int counterPredicate = 0;

			for (int j = 0; j < selectVar; j++)
			{
				int k = proj2temp[j];
				if (k != -1)
				{
					if (k < id_cols)
					{
						if (isel[k])
						{
							for (unsigned i = 0; i < retAnsNum; i++)
							{
								unsigned ans_id = result0.result[i].id[k];
								if (ans_id == INVALID)
									continue;
								if (ans_id < Util::LITERAL_FIRST_ID)
									counterEntity++;
								else
									counterLiteral++;
							}
						}
						else
							for (unsigned i = 0; i < retAnsNum; i++)
							{
								unsigned ans_id = result0.result[i].id[k];
								if (ans_id == INVALID)
									continue;
								counterPredicate++;
							}
					}
				}
			}
			a[0]->request_reserve(counterEntity);
			a[1]->request_reserve(counterLiteral);
			a[2]->request_reserve(counterPredicate);*/
			
			ret_result.delete_another_way = 1;
			string *t = new string[retAnsNum*selectVar];
			for (unsigned int i = 0,off =0 ; i < retAnsNum; i++, off += selectVar)
				ret_result.answer[i] = t + off;

			//a[0]->set_string_base(t);
			//a[1]->set_string_base(t);
			//a[2]->set_string_base(t);

			//write index lock
			for (unsigned j = 0; j < selectVar; j++)
			{
				int k = proj2temp[j];
				if (k != -1)
				{
					if (k < id_cols)
					{
						if (isel[k])
						{
							for (unsigned i = 0; i < retAnsNum; i++)
							{
								unsigned ans_id = result0.result[i].id[k];
								if (ans_id != INVALID)
								{
									if (ans_id < Util::LITERAL_FIRST_ID)
										//a[0]->addRequest
										requestVectors[0].push_back(StringIndexFile::AccessRequest(ans_id, i*selectVar + j));
									else
										//a[1]->addRequest(ans_id - Util::LITERAL_FIRST_ID , i*selectVar + j);
										requestVectors[1].push_back(StringIndexFile::AccessRequest(ans_id - Util::LITERAL_FIRST_ID, i*selectVar + j));
								}
							}
						}
						else
						{
							for (unsigned i = 0; i < retAnsNum; i++)
							{
								unsigned ans_id = result0.result[i].id[k];
								if (ans_id != INVALID){
									//a[2]->addRequest(ans_id, i*selectVar + j);
									requestVectors[2].push_back(StringIndexFile::AccessRequest(ans_id, i*selectVar + j));
								}
							}
						}
					}
					else
					{
						for (unsigned i = 0; i < retAnsNum; i++)
						{
							ret_result.answer[i][j] = result0.result[i].str[k - id_cols];
							// Up to this point the backslashes are hidden
						}
					}
				}
			}
			//cout << "in getFinal Result the first half use " << Util::get_cur_time() - t0 << "  ms" << endl;
			//pthread_join(tidp, NULL);
			this->stringindex->trySequenceAccess(requestVectors,t, true, -1);
			//write index unlock
		}
		else
		{
			for (unsigned i = 0; i < ret_result.ansNum; i++)
				for (int j = 0; j < ret_result.select_var_num; j++)
				{
					int k = proj2temp[j];
					if (k != -1)
					{
						if (k < id_cols)
						{
							string ans_str;

							unsigned ans_id = result0.result[i].id[k];
							if (ans_id != INVALID)
							{
								this->stringindex->randomAccess(ans_id, &ans_str, isel[k]);
							}
							ret_result.writeToStream(ans_str);
						}
						else
						{
							string ans_str = result0.result[i].str[k - id_cols];
							ret_result.writeToStream(ans_str);
						}
					}
				}

			ret_result.resetStream();
		}
	}

	else if (this->query_tree.getQueryForm() == QueryTree::Ask_Query)
	{
		ret_result.true_select_var_num = ret_result.select_var_num = 1;
		ret_result.setVar(vector<string>(1, "?_askResult"));
		ret_result.ansNum = 1;

		if (!ret_result.checkUseStream())
		{
			ret_result.answer = new string*[ret_result.ansNum];
			ret_result.answer[0] = new string[ret_result.select_var_num];

			ret_result.answer[0][0] = "\"false\"^^<http://www.w3.org/2001/XMLSchema#boolean>";
			for (int i = 0; i < (int)this->temp_result->results.size(); i++)
				if (!this->temp_result->results[i].result.empty())
				{
					if (this->temp_result->results[i].result[0].str.size() == 1 \
						&& this->temp_result->results[i].result[0].str[0] == "false")
						continue;
					ret_result.answer[0][0] = "\"true\"^^<http://www.w3.org/2001/XMLSchema#boolean>";
					break;
				}
		}
	}

	this->releaseResult();
}

void GeneralEvaluation::releaseResult()
{
	if (this->temp_result == NULL)
		return;

	this->temp_result->release();
	delete this->temp_result;
	this->temp_result = NULL;
}

void GeneralEvaluation::prepareUpdateTriple(QueryTree::GroupPattern &update_pattern, TripleWithObjType *&update_triple, TYPE_TRIPLE_NUM &update_triple_num)
{
	update_pattern.getVarset();

	if (update_triple != NULL)
	{
		delete[] update_triple;
		update_triple = NULL;
	}

	if (this->temp_result == NULL)
		return;

	update_triple_num = 0;

	for (int i = 0; i < (int)update_pattern.sub_group_pattern.size(); i++)
		if (update_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
		{
			for (int j = 0; j < (int)this->temp_result->results.size(); j++)
				if (update_pattern.sub_group_pattern[i].pattern.varset.belongTo(this->temp_result->results[j].getAllVarset()))
					update_triple_num += this->temp_result->results[j].result.size();
		}

	update_triple = new TripleWithObjType[update_triple_num];

	int update_triple_count = 0;
	for (int i = 0; i < (int)update_pattern.sub_group_pattern.size(); i++)
		if (update_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
		{
			for (int j = 0; j < (int)this->temp_result->results.size(); j++)
			{
				int id_cols = this->temp_result->results[j].id_varset.getVarsetSize();

				if (update_pattern.sub_group_pattern[i].pattern.varset.belongTo(this->temp_result->results[j].getAllVarset()))
				{
					int subject_id = -1, predicate_id = -1, object_id = -1;
					if (update_pattern.sub_group_pattern[i].pattern.subject.value[0] == '?')
						subject_id = Varset(update_pattern.sub_group_pattern[i].pattern.subject.value).mapTo(this->temp_result->results[j].getAllVarset())[0];
					if (update_pattern.sub_group_pattern[i].pattern.predicate.value[0] == '?')
						predicate_id = Varset(update_pattern.sub_group_pattern[i].pattern.predicate.value).mapTo(this->temp_result->results[j].getAllVarset())[0];
					if (update_pattern.sub_group_pattern[i].pattern.object.value[0] == '?')
						object_id = Varset(update_pattern.sub_group_pattern[i].pattern.object.value).mapTo(this->temp_result->results[j].getAllVarset())[0];

					string subject, predicate, object;
					TripleWithObjType::ObjectType object_type;

					if (subject_id == -1)
						subject = update_pattern.sub_group_pattern[i].pattern.subject.value;
					if (predicate_id == -1)
						predicate = update_pattern.sub_group_pattern[i].pattern.predicate.value;
					if (object_id == -1)
						object = update_pattern.sub_group_pattern[i].pattern.object.value;

					for (int k = 0; k < (int)this->temp_result->results[j].result.size(); k++)
					{
						if (subject_id != -1)
						{
							if (subject_id < id_cols)
								this->stringindex->randomAccess(this->temp_result->results[j].result[k].id[subject_id], &subject, true);
							else
								subject = this->temp_result->results[j].result[k].str[subject_id - id_cols];
						}

						if (predicate_id != -1)
						{
							if (predicate_id < id_cols)
								this->stringindex->randomAccess(this->temp_result->results[j].result[k].id[predicate_id], &predicate, false);
							else
								predicate = this->temp_result->results[j].result[k].str[predicate_id - id_cols];
						}

						if (object_id != -1)
						{
							if (object_id < id_cols)
								this->stringindex->randomAccess(this->temp_result->results[j].result[k].id[object_id], &object, true);
							else
								object = this->temp_result->results[j].result[k].str[object_id - id_cols];
						}

						if (object[0] == '<')
							object_type = TripleWithObjType::Entity;
						else
							object_type = TripleWithObjType::Literal;

						update_triple[update_triple_count++] = TripleWithObjType(subject, predicate, object, object_type);
					}
				}
			}
		}
}

void
GeneralEvaluation::pathVec2JSON(int src, int dst, const vector<int> &v, stringstream &ss)
{
	unordered_map<int, string> nodeIRI;
	ss << "{\"src\":\"" << kvstore->getStringByID(src) \
		<< "\",\"dst\":\"" << kvstore->getStringByID(dst) << "\",";
	ss << "\"edges\":[";
	int vLen = v.size();
	for (int i = 0; i < vLen; i++)
	{
		if (i % 2 == 0)	// Node
			nodeIRI[v[i]] = kvstore->getStringByID(v[i]);
		else	// Edge
		{
			ss << "{\"fromNode\":" << v[i - 1] << ",\"toNode\":" << v[i + 1] \
				<< ",\"predIRI\":\"" << kvstore->getPredicateByID(v[i]) << "\"}";
			if (i != vLen - 2)	// Not the last edge
				ss << ",";
		}
	}
	ss << "],\"nodes\":[";	// End of edges, start of nodes
	for (auto it = nodeIRI.begin(); it != nodeIRI.end(); ++it)
	{
		ss << "{\"nodeIndex\":" << it->first << ",\"nodeIRI\":\"" << it->second << "\"}";
		// if (it != nodeIRI.end() - 1)
		// 	ss << ",";
		if (next(it) != nodeIRI.end())
			ss << ",";
	}
	ss << "]}";
}

int GeneralEvaluation::constructTriplePattern(QueryTree::GroupPattern& triple_pattern, int dep)
{
	int group_pattern_triple_num = 0;
	// Extract all sub-group-patterns that are themselves triples, add into triple_pattern //
	for (int j = 0; j < (int)(rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern.size()); j++)
		if (rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[j].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
		{
			triple_pattern.addOnePattern(QueryTree::GroupPattern::Pattern(
				QueryTree::GroupPattern::Pattern::Element(rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[j].pattern.subject.value),
				QueryTree::GroupPattern::Pattern::Element(rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[j].pattern.predicate.value),
				QueryTree::GroupPattern::Pattern::Element(rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[j].pattern.object.value)
			));
			group_pattern_triple_num++;
		}
	triple_pattern.getVarset();
	// Add extra triple patterns from shallower levels of rewriting_evaluation_stack //
	if (dep > 0)
	{
		Varset need_add;

		/// Construct var_count: map from variable name to #occurrence in triple_pattern ///
		map<string, int> var_count;
		for (int j = 0; j < (int)triple_pattern.sub_group_pattern.size(); j++)
			if (triple_pattern.sub_group_pattern[j].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
			{
				string subject = triple_pattern.sub_group_pattern[j].pattern.subject.value;
				string object = triple_pattern.sub_group_pattern[j].pattern.object.value;

				if (subject[0] == '?')
				{
					if (var_count.count(subject) == 0)
						var_count[subject] = 0;
					var_count[subject]++;
				}
				if (object[0] == '?')
				{
					if (var_count.count(object) == 0)
						var_count[object] = 0;
					var_count[object]++;
				}
			}

		/// Construct need_add: all variable that occur exactly once ///
		for (map<string, int>::iterator iter = var_count.begin(); iter != var_count.end(); iter++)
			if (iter->second == 1)
				need_add.addVar(iter->first);

		/// Add triples from shallower levels of rewriting_evaluation_stack that ///
		/// - have common variables with need_add ///
		/// - have varset size == 1 OR 2 ///
		/// (After adding, subtract the covered var from need_add) ///
		/// QUESTION: Why first 1 then 2? ///
		for (int j = 0; j < dep; j++)
		{
			QueryTree::GroupPattern &parrent_group_pattern = this->rewriting_evaluation_stack[j].group_pattern;

			for (int k = 0; k < (int)parrent_group_pattern.sub_group_pattern.size(); k++)
				if (parrent_group_pattern.sub_group_pattern[k].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
					if (need_add.hasCommonVar(parrent_group_pattern.sub_group_pattern[k].pattern.subject_object_varset) &&
						parrent_group_pattern.sub_group_pattern[k].pattern.varset.getVarsetSize() == 1)	// Only difference here: 1
					{
						triple_pattern.addOnePattern(QueryTree::GroupPattern::Pattern(
							QueryTree::GroupPattern::Pattern::Element(parrent_group_pattern.sub_group_pattern[k].pattern.subject.value),
							QueryTree::GroupPattern::Pattern::Element(parrent_group_pattern.sub_group_pattern[k].pattern.predicate.value),
							QueryTree::GroupPattern::Pattern::Element(parrent_group_pattern.sub_group_pattern[k].pattern.object.value)
						));
						need_add = need_add - parrent_group_pattern.sub_group_pattern[k].pattern.subject_object_varset;
					}
		}
		for (int j = 0; j < dep; j++)
		{
			QueryTree::GroupPattern &parrent_group_pattern = this->rewriting_evaluation_stack[j].group_pattern;

			for (int k = 0; k < (int)parrent_group_pattern.sub_group_pattern.size(); k++)
				if (parrent_group_pattern.sub_group_pattern[k].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
					if (need_add.hasCommonVar(parrent_group_pattern.sub_group_pattern[k].pattern.subject_object_varset) &&
						parrent_group_pattern.sub_group_pattern[k].pattern.varset.getVarsetSize() == 2)	// Only difference here: 2
					{
						triple_pattern.addOnePattern(QueryTree::GroupPattern::Pattern(
							QueryTree::GroupPattern::Pattern::Element(parrent_group_pattern.sub_group_pattern[k].pattern.subject.value),
							QueryTree::GroupPattern::Pattern::Element(parrent_group_pattern.sub_group_pattern[k].pattern.predicate.value),
							QueryTree::GroupPattern::Pattern::Element(parrent_group_pattern.sub_group_pattern[k].pattern.object.value)
						));
						need_add = need_add - parrent_group_pattern.sub_group_pattern[k].pattern.subject_object_varset;
					}
		}
	}
	triple_pattern.getVarset();

	return group_pattern_triple_num;
}

void GeneralEvaluation::getUsefulVarset(Varset& useful, int dep)
{
	// Vars that are SELECT, GROUP BY, or ORDER BY //
	useful = this->query_tree.getResultProjectionVarset() + this->query_tree.getGroupByVarset() \
				+ this->query_tree.getOrderByVarset();
	if (!this->query_tree.checkProjectionAsterisk())
	{
		// All vars from ancestor levels' triples and filters //
		for (int j = 0; j < dep; j++)
		{
			QueryTree::GroupPattern &parrent_group_pattern = this->rewriting_evaluation_stack[j].group_pattern;

			for (int k = 0; k < (int)parrent_group_pattern.sub_group_pattern.size(); k++)
			{
				if (parrent_group_pattern.sub_group_pattern[k].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
					useful += parrent_group_pattern.sub_group_pattern[k].pattern.varset;
				else if (parrent_group_pattern.sub_group_pattern[k].type == QueryTree::GroupPattern::SubGroupPattern::Filter_type)
					useful += parrent_group_pattern.sub_group_pattern[k].filter.varset;
			}
		}
		// All vars from current levels' OPTIONAL and filters //
		for (int j = 0; j < (int)(rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern.size()); j++)
		{
			if (rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[j].type == QueryTree::GroupPattern::SubGroupPattern::Optional_type)
				useful += rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[j].optional.group_pattern_resultset_maximal_varset;
			else if (rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[j].type == QueryTree::GroupPattern::SubGroupPattern::Filter_type)
				useful += rewriting_evaluation_stack[dep].group_pattern.sub_group_pattern[j].filter.varset;
		}
	}
}

bool GeneralEvaluation::checkBasicQueryCache(vector<QueryTree::GroupPattern::Pattern>& basic_query, TempResultSet *&sub_result, Varset& useful)
{
	bool success = false;
	if (this->query_cache != NULL)
	{
		TempResultSet *temp = new TempResultSet();
		temp->results.push_back(TempResult());
		long tv_bfcheck = Util::get_cur_time();
		success = this->query_cache->checkCached(basic_query, useful, temp->results[0]);
		long tv_afcheck = Util::get_cur_time();
		printf("after checkCache, used %ld ms.\n", tv_afcheck - tv_bfcheck);

		// If query cache hit, save partial result //
		// Note the semantics of doJoin: sub_result is joined with temp, and saved into new_result //
		if (success)
		{
			printf("QueryCache hit\n");
			printf("Final result size: %d\n", (int)temp->results[0].result.size());

			TempResultSet *new_result = new TempResultSet();
			sub_result->doJoin(*temp, *new_result, this->stringindex, this->query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset);

			sub_result->release();
			delete sub_result;

			sub_result = new_result;
		}
		else
			printf("QueryCache miss\n");

		temp->release();
		delete temp;
	}
	return success;
}

int GeneralEvaluation::fillCandList(shared_ptr<BGPQuery> bgp_query, int dep, vector<string> &occur)
{
	int curr_dep = dep - 1;
	while (curr_dep >= 0 && (this->rewriting_evaluation_stack[curr_dep].result == NULL \
		|| this->rewriting_evaluation_stack[curr_dep].result->results.size() == 0))
		curr_dep--;

	if (curr_dep < 0)
		return -1;
	
	TempResultSet *&last_result = this->rewriting_evaluation_stack[curr_dep].result;

	for (size_t k = 0; k < occur.size(); k++)
	{
		/// For each var in the current BGP ///
		/// construct result_set: the candidate values of this var from the parent result in rewriting_evaluation_stack ///
		set<unsigned> result_set;

		for (size_t t = 0; t < last_result->results.size(); t++)
		{
			//// For each var in each BGP, check each result in the parent result in rewriting_evaluation_stack ////
			//// If this var exists in the parent result, retrieve its value in the parent result into result_set ////
			vector<TempResult::ResultPair> &result = last_result->results[t].result;

			int pos = Varset(occur[k]).mapTo(last_result->results[t].id_varset)[0];
			if (pos != -1)
			{
				for (int l = 0; l < (int)result.size(); l++)
					result_set.insert(result[l].id[pos]);
			}
		}

		/// If result_set is non-empty, set it as the current var's candidate list ///
		if (!result_set.empty())
		{
			IDList result_vector;
			result_vector.reserve(result_set.size());

			for (set<unsigned>::iterator iter = result_set.begin(); iter != result_set.end(); iter++)
				result_vector.addID(*iter);

			
			bgp_query->set_var_candidate_cache(bgp_query->get_var_id_by_name(occur[k]), make_shared<IDList>(result_vector));

			printf("fill var %s CandidateList size %d\n", occur[k].c_str(), (int)result_vector.size());
		}
	}
	return curr_dep;
}

void GeneralEvaluation::fillCandList(vector<shared_ptr<BGPQuery>>& bgp_query_vec, int dep, vector<vector<string> >& encode_varset)
{
	if (dep <= 0)
		return;
	
	for (int j = 0; j < bgp_query_vec.size(); j++)
	{
		vector<string> &basic_query_encode_varset = encode_varset[j];
		fillCandList(bgp_query_vec[j], dep, basic_query_encode_varset);
	}
}

void GeneralEvaluation::fillCandList(SPARQLquery& sparql_query, int dep, vector<vector<string> >& encode_varset)
{
	if (dep <= 0)
		return;
	
	TempResultSet *&last_result = this->rewriting_evaluation_stack[dep - 1].result;

	for (int j = 0; j < sparql_query.getBasicQueryNum(); j++)
	{
		BasicQuery &basic_query = sparql_query.getBasicQuery(j);
		vector<string> &basic_query_encode_varset = encode_varset[j];

		for (int k = 0; k < (int)basic_query_encode_varset.size(); k++)
		{
			/// For each var in the current BGP ///
			/// construct result_set: the candidate values of this var from the parent result in rewriting_evaluation_stack ///
			set<unsigned> result_set;

			for (int t = 0; t < (int)last_result->results.size(); t++)
			{
				//// For each var in each BGP, check each result in the parent result in rewriting_evaluation_stack ////
				//// If this var exists in the parent result, retrieve its value in the parent result into result_set ////
				vector<TempResult::ResultPair> &result = last_result->results[t].result;

				int pos = Varset(basic_query_encode_varset[k]).mapTo(last_result->results[t].id_varset)[0];
				if (pos != -1)
				{
					for (int l = 0; l < (int)result.size(); l++)
						result_set.insert(result[l].id[pos]);
				}
			}

			/// If result_set is non-empty, set it as the current var's candidate list ///
			if (!result_set.empty())
			{
				vector<unsigned> result_vector;
				result_vector.reserve(result_set.size());

				for (set<unsigned>::iterator iter = result_set.begin(); iter != result_set.end(); iter++)
					result_vector.push_back(*iter);


				basic_query.getCandidateList(basic_query.getIDByVarName(basic_query_encode_varset[k])).copy(result_vector);
				basic_query.setReady(basic_query.getIDByVarName(basic_query_encode_varset[k]));

				printf("fill var %s CandidateList size %d\n", basic_query_encode_varset[k].c_str(), (int)result_vector.size());
			}
		}
	}
}

void GeneralEvaluation::joinBasicQueryResult(SPARQLquery& sparql_query, TempResultSet *new_result, TempResultSet *sub_result, vector<vector<string> >& encode_varset, \
	vector<vector<QueryTree::GroupPattern::Pattern> >& basic_query_handle, long tv_begin, long tv_handle, int dep)
{
	// Each BGP's results are copied out to temp, and then joined with sub_result //
	for (int j = 0; j < sparql_query.getBasicQueryNum(); j++)
	{
		TempResultSet *temp = new TempResultSet();
		temp->results.push_back(TempResult());

		temp->results[0].id_varset = Varset(encode_varset[j]);
		int varnum = (int)encode_varset[j].size();

		vector<unsigned*> &basicquery_result = sparql_query.getBasicQuery(j).getResultList();
		int basicquery_result_num = (int)basicquery_result.size();

		temp->results[0].result.reserve(basicquery_result_num);
		for (int k = 0; k < basicquery_result_num; k++)
		{
			unsigned *v = new unsigned[varnum];
			memcpy(v, basicquery_result[k], sizeof(int) * varnum);
			temp->results[0].result.push_back(TempResult::ResultPair());
			temp->results[0].result.back().id = v;
			temp->results[0].result.back().sz = varnum;
		}

		if (this->query_cache != NULL && dep == 0)
		{
			//if unconnected, time is incorrect
			int time = tv_handle - tv_begin;

			long tv_bftry = Util::get_cur_time();
			bool success = this->query_cache->tryCaching(basic_query_handle[j], temp->results[0], time);
			if (success)	printf("QueryCache cached\n");
			else			printf("QueryCache didn't cache\n");
			long tv_aftry = Util::get_cur_time();
			printf("during tryCache, used %ld ms.\n", tv_aftry - tv_bftry);
		}

		if (sub_result->results.empty())
		{
			delete sub_result;
			sub_result = temp;
		}
		else
		{
			// TempResultSet *new_result = new TempResultSet();
			new_result = new TempResultSet();
			sub_result->doJoin(*temp, *new_result, this->stringindex, this->query_tree.getGroupPattern().group_pattern_subject_object_maximal_varset);

			temp->release();
			sub_result->release();
			delete temp;
			delete sub_result;

			sub_result = new_result;
		}
	}

	printf("In joinBasicQueryResult, print varset\n");
	sub_result->results[0].getAllVarset();
	printf("11111\n");
}

void GeneralEvaluation::addAllTriples(const QueryTree::GroupPattern &group_pattern)
{
	for (size_t i = 0; i < group_pattern.sub_group_pattern.size(); i++)
	{
		if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Group_type)
			addAllTriples(group_pattern.sub_group_pattern[i].group_pattern);
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Union_type)
		{
			for (size_t j = 0; j < group_pattern.sub_group_pattern[i].unions.size(); j++)
				addAllTriples(group_pattern.sub_group_pattern[i].unions[j]);
		}
		else if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Optional_type \
			|| group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Minus_type)
			addAllTriples(group_pattern.sub_group_pattern[i].optional);
	}

	for (size_t i = 0; i < group_pattern.sub_group_pattern.size(); i++)
	{
		// Here not yet transformed into BGP_type
		if (group_pattern.sub_group_pattern[i].type == QueryTree::GroupPattern::SubGroupPattern::Pattern_type)
		{
			bgp_query_total->AddTriple(Triple(group_pattern.sub_group_pattern[i].pattern.subject.value, \
					group_pattern.sub_group_pattern[i].pattern.predicate.value, \
					group_pattern.sub_group_pattern[i].pattern.object.value));
		}
	}
}