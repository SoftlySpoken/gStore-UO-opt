/*=============================================================================
# Filename: PlanGenerator.cpp
# Author: Linglin Yang
# Mail: linglinyang@stu.pku.edu.cn
=============================================================================*/

#include "PlanGenerator.h"

PlanGenerator::PlanGenerator(KVstore *kvstore_, BasicQuery *basicquery_, Statistics *statistics_, IDCachesSharePtr& id_caches_):
					kvstore(kvstore_), basicquery(basicquery_), statistics(statistics_), id_caches(id_caches_){};

PlanGenerator::PlanGenerator(KVstore *kvstore_, BGPQuery *bgpquery_, Statistics *statistics_, IDCachesSharePtr &id_caches_):
					kvstore(kvstore_), bgpquery(bgpquery_), statistics(statistics_), id_caches(id_caches_){};

unsigned PlanGenerator::get_sample_size(unsigned id_cache_size){
	if(id_cache_size <= 100){
		return SAMPLE_CACHE_MAX;
	} else{
		return (unsigned )(log(id_cache_size)*11);
	}
}

////  Copied from Database::exist_triple in Database.cpp
////  If there exist a triple (s_id, p_id, o_id) in db, return true;
////  else, return false
bool PlanGenerator::check_exist_this_triple(TYPE_ENTITY_LITERAL_ID s_id, TYPE_PREDICATE_ID p_id, TYPE_ENTITY_LITERAL_ID o_id){

	unsigned* _objidlist = nullptr;
	unsigned _list_len = 0;
	bool is_exist = false;
	//get exclusive before update!
	kvstore->getobjIDlistBysubIDpreID(s_id, p_id, _objidlist, _list_len, true);

	if (Util::bsearch_int_uporder(o_id, _objidlist, _list_len) != INVALID){
		is_exist = true;
	}
	delete[] _objidlist;

	return is_exist;
}



bool PlanGenerator::check_pass(vector<int> &linked_nei_pos, vector<char> &edge_type,
						   vector<TYPE_PREDICATE_ID> &p_list, vector<unsigned> &last_sample, unsigned this_var_sample){
	for(unsigned i = 0; i < linked_nei_pos.size(); ++i){
		if(edge_type[i] == Util::EDGE_IN){
			if(!check_exist_this_triple(last_sample[linked_nei_pos[i]], p_list[i], this_var_sample)){
				return false;
			}
		} else{
			if(!check_exist_this_triple(this_var_sample, p_list[i], last_sample[linked_nei_pos[i]])){
				return false;
			}
		}

	}
	return true;

}

unsigned long PlanGenerator::card_estimator(const vector<int> &last_plan_nodes, int next_join_node, const vector<int> &now_plan_nodes,
										bool use_sample){


	unsigned now_plan_node_num = now_plan_nodes.size();

	if(use_sample == false) {

		if (now_plan_node_num == 2) {

			unsigned long card_estimation = ULONG_MAX;

			unsigned long all_num = statistics->get_type_num_by_type_id(var_to_type_map[next_join_node]) *
					statistics->get_type_num_by_type_id(var_to_type_map[last_plan_nodes[0]]);

			for (int i = 0; i < basicquery->getVarDegree(next_join_node); ++i) {
				if (basicquery->getEdgeNeighborID(next_join_node, i) == last_plan_nodes[0]) {
					char in_or_out = basicquery->getEdgeType(next_join_node, i);
					TYPE_PREDICATE_ID p_id = basicquery->getEdgePreID(next_join_node, i);

					double selectivity = var_to_num_map[next_join_node] * var_to_num_map[last_plan_nodes[0]] / ((double) all_num);

					if(in_or_out == Util::EDGE_IN){
						card_estimation = min(card_estimation,
											  (unsigned long)(selectivity*statistics->get_type_one_edge_typeid_num_by_id(var_to_type_map[last_plan_nodes[0]], p_id, var_to_type_map[next_join_node])));
					} else{
						card_estimation = min(card_estimation,
											  (unsigned long)(selectivity*statistics->get_type_one_edge_typeid_num_by_id(var_to_type_map[next_join_node], p_id, var_to_type_map[last_plan_nodes[0]])));
					}
				}
			}

			if(card_estimation == ULONG_MAX){
				cout << "error when estimate cardinality when var_num = 2" << endl;
				exit(-1);
			} else{
				if(card_cache.size() < 1){
					map<vector<int>, unsigned long> two_var_card_map;
					two_var_card_map.insert(make_pair(now_plan_nodes, card_estimation));
					card_cache.push_back(two_var_card_map);

				} else{
					card_cache[0].insert(make_pair(now_plan_nodes, card_estimation));
				}
				return card_estimation;
			}
		}

		if (now_plan_node_num >= 3) {

			unsigned long card_estimation = ULONG_MAX;

			unsigned long last_plan_card = card_cache[now_plan_node_num-3][last_plan_nodes];
			if(now_plan_node_num==3)
				cout << "last_plan_card for " << last_plan_nodes[0] << " " << last_plan_nodes[1] << " " << next_join_node<<"  "<< last_plan_card<<endl;

			for (int i = 0; i < basicquery->getVarDegree(next_join_node); ++i) {
				if (find(last_plan_nodes.begin(), last_plan_nodes.end(),
						 basicquery->getEdgeNeighborID(next_join_node, i)) != last_plan_nodes.end()) {

					char in_or_out = basicquery->getEdgeType(next_join_node, i);
					TYPE_PREDICATE_ID p_id = basicquery->getEdgePreID(next_join_node, i);

					double selectivity = var_to_num_map[next_join_node] * var_to_num_map[basicquery->getEdgeNeighborID(next_join_node, i)] /
							((double)(statistics->get_type_num_by_type_id(var_to_type_map[next_join_node]) *
							statistics->get_type_num_by_type_id(var_to_type_map[basicquery->getEdgeNeighborID(next_join_node, i)])));

					if(in_or_out == Util::EDGE_IN){
						card_estimation = min(card_estimation,
											  (unsigned long)(last_plan_card*selectivity*statistics->get_type_one_edge_typeid_num_by_id(var_to_type_map[basicquery->getEdgeNeighborID(next_join_node, i)], p_id, var_to_type_map[next_join_node])));
					} else{
						card_estimation = min(card_estimation,
											  (unsigned long)(last_plan_card*selectivity*statistics->get_type_one_edge_typeid_num_by_id(var_to_type_map[next_join_node], p_id, var_to_type_map[basicquery->getEdgeNeighborID(next_join_node, i)])));
					}
				}
			}
			cout << "now card estimation: "<<card_estimation<<endl;

			if(card_estimation == ULONG_MAX){
				cout << "error when estimate cardinality when var_num = 2" << endl;
				exit(-1);
			} else{
				if(card_cache.size() < now_plan_node_num-1){
					map<vector<int>, unsigned long> this_var_num_card_map;
					this_var_num_card_map.insert(make_pair(now_plan_nodes, card_estimation));
					card_cache.push_back(this_var_num_card_map);

				} else{
					card_cache[now_plan_node_num-2].insert(make_pair(now_plan_nodes, card_estimation));
				}
				return card_estimation;
			}
		}

	}
	else{

		if(now_plan_node_num == 2) {

			if (card_cache.size()==0 || card_cache[0].find(now_plan_nodes) == card_cache[0].end()) {

				unsigned long card_estimation;
				vector<vector<unsigned>> this_sample;

				unsigned now_sample_num = 0;

				vector<int> linked_nei_id;
				vector<char> edge_type;
				vector<TYPE_PREDICATE_ID> p_list;

				for (int i = 0; i < basicquery->getVarDegree(next_join_node); ++i) {
					if (basicquery->getEdgeNeighborID(next_join_node, i) == last_plan_nodes[0]) {
						linked_nei_id.push_back(i);
						edge_type.push_back(basicquery->getEdgeType(next_join_node, i));
						p_list.push_back(basicquery->getEdgePreID(next_join_node, i));
					}
				}


				map<int, int> new_id_pos_map;
				if (last_plan_nodes[0] < next_join_node) {
					new_id_pos_map[last_plan_nodes[0]] = 0;
					new_id_pos_map[next_join_node] = 1;
				} else {
					new_id_pos_map[next_join_node] = 0;
					new_id_pos_map[last_plan_nodes[0]] = 1;
				}

				unsigned long s_o_list1_total_num = 0;
				unsigned long s_o_list2_total_num = 0;

				random_device rd;
				mt19937 eng(rd());
				uniform_real_distribution<double> dis(0, 1);


				if (edge_type[0] == Util::EDGE_IN) {
					// not need to sample, because sampled in considering all scans
					for (unsigned i = 0; i < var_to_sample_cache[last_plan_nodes[0]].size(); ++i) {
						unsigned *s_o_list = nullptr;
						unsigned s_o_list_len = 0;

						kvstore->getobjIDlistBysubIDpreID(var_to_sample_cache[last_plan_nodes[0]][i], p_list[0],
															s_o_list,
															s_o_list_len);


						s_o_list1_total_num += s_o_list_len;

						for (unsigned j = 0; j < s_o_list_len; ++j) {
							if (binary_search((*id_caches)[next_join_node]->begin(),
											  (*id_caches)[next_join_node]->end(),
											  s_o_list[j])) {
								unsigned k = 1;
								for (; k < edge_type.size(); ++k) {
									if (edge_type[k] == Util::EDGE_IN) {
										if (!check_exist_this_triple(var_to_sample_cache[last_plan_nodes[0]][i],
																	 p_list[k],
																	 s_o_list[j])) {
											break;
										}
									} else {
										if (!check_exist_this_triple(s_o_list[j], p_list[k],
																	 var_to_sample_cache[last_plan_nodes[0]][i])) {
											break;
										}
									}

								}

								if (k == edge_type.size()) {
									++now_sample_num;
									if (now_sample_num < SAMPLE_CACHE_MAX) {
										//
										vector<unsigned> this_pass_sample(2);
										this_pass_sample[new_id_pos_map[last_plan_nodes[0]]] = var_to_sample_cache[last_plan_nodes[0]][i];
										this_pass_sample[new_id_pos_map[next_join_node]] = s_o_list[j];

										this_sample.push_back(move(this_pass_sample));
									} else {
										if (dis(eng) < SAMPLE_PRO) {

											vector<unsigned> this_pass_sample(2);
											this_pass_sample[new_id_pos_map[last_plan_nodes[0]]] = var_to_sample_cache[last_plan_nodes[0]][i];
											this_pass_sample[new_id_pos_map[next_join_node]] = s_o_list[j];

											this_sample.push_back(move(this_pass_sample));

										}
									}
								}

							}
						}

						delete[] s_o_list;


					}

					for (unsigned i = 0; i < var_to_sample_cache[next_join_node].size(); ++i) {
						unsigned *s_o_list = nullptr;
						unsigned s_o_list_len = 0;

						kvstore->getsubIDlistByobjIDpreID(var_to_sample_cache[next_join_node][i], p_list[0], s_o_list,
															s_o_list_len);
						s_o_list2_total_num += s_o_list_len;

						delete[] s_o_list;


					}

					if (s_o_list_average_size.find(last_plan_nodes[0]) == s_o_list_average_size.end()) {
						map<int, unsigned > this_node_selectivity_map;
						this_node_selectivity_map.insert(
								make_pair(next_join_node, (unsigned )((double) s_o_list1_total_num / var_to_sample_cache[last_plan_nodes[0]].size())));
						s_o_list_average_size.insert(make_pair(last_plan_nodes[0], this_node_selectivity_map));
					} else {
						s_o_list_average_size[last_plan_nodes[0]].insert(
								make_pair(next_join_node, (unsigned )((double) s_o_list1_total_num / var_to_sample_cache[last_plan_nodes[0]].size())));
					}

					if (s_o_list_average_size.find(next_join_node) == s_o_list_average_size.end()) {
						map<int, unsigned > this_node_selectivity_map;
						this_node_selectivity_map.insert(
								make_pair(last_plan_nodes[0], (unsigned )((double) s_o_list2_total_num / var_to_sample_cache[next_join_node].size())));
						s_o_list_average_size.insert(make_pair(next_join_node, this_node_selectivity_map));
					} else {
						s_o_list_average_size[next_join_node].insert(
								make_pair(last_plan_nodes[0], (unsigned )((double) s_o_list2_total_num / var_to_sample_cache[next_join_node].size())));
					}

				} else {


					for (unsigned i = 0; i < var_to_sample_cache[next_join_node].size(); ++i) {
						unsigned *s_o_list = nullptr;
						unsigned s_o_list_len = 0;

						kvstore->getobjIDlistBysubIDpreID(var_to_sample_cache[next_join_node][i], p_list[0], s_o_list,
															s_o_list_len);

						s_o_list1_total_num += s_o_list_len;

						for (unsigned j = 0; j < s_o_list_len; ++j) {
							if (binary_search((*id_caches)[last_plan_nodes[0]]->begin(),
											  (*id_caches)[last_plan_nodes[0]]->end(), s_o_list[j])) {
								unsigned k = 1;
								for (; k < edge_type.size(); ++k) {
									if (edge_type[k] == Util::EDGE_IN) {
										if (!check_exist_this_triple(s_o_list[j], p_list[k],
																	 var_to_sample_cache[next_join_node][i])) {
											break;
										}
									} else {
										if (!check_exist_this_triple(var_to_sample_cache[next_join_node][i], p_list[k],
																	 s_o_list[j])) {
											break;
										}
									}

								}

								if (k == edge_type.size()) { ;
									++now_sample_num;
									if (now_sample_num < SAMPLE_CACHE_MAX) {
										//
										vector<unsigned> this_pass_sample(2);
										this_pass_sample[new_id_pos_map[last_plan_nodes[0]]] = s_o_list[j];
										this_pass_sample[new_id_pos_map[next_join_node]] = var_to_sample_cache[next_join_node][i];

										this_sample.push_back(move(this_pass_sample));
									} else {
										if (dis(eng) < SAMPLE_PRO) {

											vector<unsigned> this_pass_sample(2);
											this_pass_sample[new_id_pos_map[last_plan_nodes[0]]] = s_o_list[j];
											this_pass_sample[new_id_pos_map[next_join_node]] = var_to_sample_cache[next_join_node][i];

											this_sample.push_back(move(this_pass_sample));

										}
									}
								}

							}
						}

						delete[] s_o_list;
					}

					for (unsigned i = 0; i < var_to_sample_cache[last_plan_nodes[0]].size(); ++i) {
						unsigned *s_o_list = nullptr;
						unsigned s_o_list_len = 0;

						kvstore->getsubIDlistByobjIDpreID(var_to_sample_cache[last_plan_nodes[0]][i], p_list[0],
															s_o_list,
															s_o_list_len);
						s_o_list2_total_num += s_o_list_len;

						delete[] s_o_list;


					}

					if (s_o_list_average_size.find(next_join_node) == s_o_list_average_size.end()) {
						map<int, unsigned > this_node_selectivity_map;
						this_node_selectivity_map.insert(
								make_pair(last_plan_nodes[0], (unsigned )((double) s_o_list1_total_num / var_to_sample_cache[next_join_node].size())));
						s_o_list_average_size.insert(make_pair(next_join_node, this_node_selectivity_map));
					} else {
						s_o_list_average_size[next_join_node].insert(
								make_pair(last_plan_nodes[0],(unsigned )((double) s_o_list1_total_num / var_to_sample_cache[next_join_node].size())));
					}

					if (s_o_list_average_size.find(last_plan_nodes[0]) == s_o_list_average_size.end()) {
						map<int, unsigned > this_node_selectivity_map;
						this_node_selectivity_map.insert(
								make_pair(next_join_node, (unsigned )((double) s_o_list2_total_num / var_to_sample_cache[last_plan_nodes[0]].size())));
						s_o_list_average_size.insert(make_pair(last_plan_nodes[0], this_node_selectivity_map));
					} else {
						s_o_list_average_size[last_plan_nodes[0]].insert(
								make_pair(next_join_node, (unsigned )((double) s_o_list2_total_num / var_to_sample_cache[last_plan_nodes[0]].size())));
					}

				}


				card_estimation = max((edge_type[0] == Util::EDGE_IN) ? (unsigned long) ((double) (now_sample_num *
						var_to_num_map[last_plan_nodes[0]]) /
								var_to_sample_cache[last_plan_nodes[0]].size())
										:
										(unsigned long) ((double) (now_sample_num *
										var_to_num_map[next_join_node]) /
										var_to_sample_cache[next_join_node].size() )
										, (unsigned long) 1);

				if (card_cache.size() < 1) {
					map<vector<int>, unsigned long> this_var_num_card_map;
					this_var_num_card_map.insert(make_pair(now_plan_nodes, card_estimation));
					card_cache.push_back(this_var_num_card_map);

					map<vector<int>, vector<vector<unsigned>>> this_var_num_sample_map;
					this_var_num_sample_map.insert(make_pair(now_plan_nodes, this_sample));
					sample_cache.push_back(this_var_num_sample_map);

				} else {
					card_cache[0].insert(make_pair(now_plan_nodes, card_estimation));
					sample_cache[0].insert(make_pair(now_plan_nodes, this_sample));
				}
				return var_to_num_map[last_plan_nodes[0]]*s_o_list_average_size[last_plan_nodes[0]][next_join_node];
			} else{
				return var_to_num_map[last_plan_nodes[0]]*s_o_list_average_size[last_plan_nodes[0]][next_join_node];//+var_to_num_map[next_join_node];

			}
		}


		if(now_plan_node_num >=3) {

			if (card_cache.size() < now_plan_node_num - 1 ||
			card_cache[now_plan_node_num - 2].find(now_plan_nodes) == card_cache[now_plan_node_num - 2].end()) {

				unsigned long card_estimation;
				unsigned long last_card_estimation = card_cache[now_plan_node_num - 3][last_plan_nodes];
				vector<vector<unsigned>> this_sample;
				vector<vector<unsigned>> &last_sample = sample_cache[now_plan_node_num - 3][last_plan_nodes];

				if (last_sample.size() != 0) {

					unsigned now_sample_num = 0;

					vector<int> linked_nei_pos;
					vector<char> edge_type;
					vector<TYPE_PREDICATE_ID> p_list;


					for (int i = 0; i < basicquery->getVarDegree(next_join_node); ++i) {
						if (find(last_plan_nodes.begin(), last_plan_nodes.end(),
								 basicquery->getEdgeNeighborID(next_join_node, i))
								 != last_plan_nodes.end()) {
							for (unsigned j = 0; j < last_plan_nodes.size(); ++j) {
								if (basicquery->getEdgeNeighborID(next_join_node, i) == last_plan_nodes[j]) {
									linked_nei_pos.push_back(j);
									break;
								}
							}

							edge_type.push_back(basicquery->getEdgeType(next_join_node, i));
							p_list.push_back(basicquery->getEdgePreID(next_join_node, i));
						}
					}

					map<int, unsigned > new_id_pos_map;
					for (unsigned i = 0, index = 0, used = 0; i < now_plan_node_num; ++i) {
						if ((index == now_plan_node_num - 1) || (!used && next_join_node < last_plan_nodes[index])) {
							new_id_pos_map[next_join_node] = i;
							used = 1;
						} else {
							new_id_pos_map[last_plan_nodes[index]] = i;
							++index;
						}
					}

					random_device rd;
					mt19937 eng(rd());
					uniform_int_distribution<unsigned > dis(0, 1000);



					if (edge_type[0] == Util::EDGE_IN) {
						for (unsigned i = 0; i < last_sample.size(); ++i) {
							unsigned *s_o_list = nullptr;
							unsigned s_o_list_len = 0;

							kvstore->getobjIDlistBysubIDpreID(last_sample[i][linked_nei_pos[0]], p_list[0], s_o_list,
																s_o_list_len);


							for (unsigned j = 0; j < s_o_list_len; ++j) {
								if (binary_search((*id_caches)[next_join_node]->begin(),
												  (*id_caches)[next_join_node]->end(),
												  s_o_list[j])) {
									unsigned k = 1;
									for (; k < edge_type.size(); ++k) {
										if (edge_type[k] == Util::EDGE_IN) {
											if (!check_exist_this_triple(last_sample[i][linked_nei_pos[k]], p_list[k],
																		 s_o_list[j])) {
												break;
											}
										} else {
											if (!check_exist_this_triple(s_o_list[j], p_list[k],
																		 last_sample[i][linked_nei_pos[k]])) {
												break;
											}
										}

									}

									if (k == edge_type.size()) { ;
										++now_sample_num;
										if (now_sample_num < SAMPLE_CACHE_MAX) {
											//
											vector<unsigned> this_pass_sample(now_plan_node_num);
											for (unsigned l = 0; l < now_plan_node_num - 1; ++l) {
												this_pass_sample[new_id_pos_map[last_plan_nodes[l]]] = last_sample[i][l];
											}
											this_pass_sample[new_id_pos_map[next_join_node]] = s_o_list[j];

											this_sample.push_back(move(this_pass_sample));
										} else {

											if (dis(eng) < SAMPLE_CACHE_MAX) {

												vector<unsigned> this_pass_sample(now_plan_node_num);
												for (unsigned l = 0; l < now_plan_node_num - 1; ++l) {
													this_pass_sample[new_id_pos_map[last_plan_nodes[l]]] = last_sample[i][l];
												}
												this_pass_sample[new_id_pos_map[next_join_node]] = s_o_list[j];

												this_sample.push_back(move(this_pass_sample));

											}
										}
									}

								}
							}

							delete[] s_o_list;


						}
					} else {
						for (unsigned i = 0; i < last_sample.size(); ++i) {
							unsigned *s_o_list = nullptr;
							unsigned s_o_list_len = 0;

							kvstore->getsubIDlistByobjIDpreID(last_sample[i][linked_nei_pos[0]], p_list[0], s_o_list,
																s_o_list_len);


							for (unsigned j = 0; j < s_o_list_len; ++j) {
								if (binary_search((*id_caches)[next_join_node]->begin(),
												  (*id_caches)[next_join_node]->end(),
												  s_o_list[j])) {
									unsigned k = 1;
									for (; k < edge_type.size(); ++k) {
										if (edge_type[k] == Util::EDGE_IN) {
											if (!check_exist_this_triple(last_sample[i][linked_nei_pos[k]], p_list[k],
																		 s_o_list[j])) {
												break;
											}
										} else {
											if (!check_exist_this_triple(s_o_list[j], p_list[k],
																		 last_sample[i][linked_nei_pos[k]])) {
												break;
											}
										}

									}

									if (k == edge_type.size()) { ;
										++now_sample_num;
										if (now_sample_num < SAMPLE_CACHE_MAX) {

											vector<unsigned> this_pass_sample(now_plan_node_num);
											for (unsigned l = 0; l < now_plan_node_num - 1; ++l) {
												this_pass_sample[new_id_pos_map[last_plan_nodes[l]]] = last_sample[i][l];
											}
											this_pass_sample[new_id_pos_map[next_join_node]] = s_o_list[j];

											this_sample.push_back(move(this_pass_sample));
										} else {
											if (dis(eng) < SAMPLE_CACHE_MAX) {

												vector<unsigned> this_pass_sample(now_plan_node_num);
												for (unsigned l = 0; l < now_plan_node_num - 1; ++l) {
													this_pass_sample[new_id_pos_map[last_plan_nodes[l]]] = last_sample[i][l];
												}
												this_pass_sample[new_id_pos_map[next_join_node]] = s_o_list[j];

												this_sample.push_back(move(this_pass_sample));

											}
										}
									}

								}
							}

							delete[] s_o_list;


						}

					}

					card_estimation = max(
							(unsigned long) ((double) (now_sample_num * last_card_estimation ) / last_sample.size() ),
							(unsigned long) 1);

				} else {
					card_estimation = 1;
				}

				if (card_cache.size() < now_plan_node_num - 1) {
					map<vector<int>, unsigned long> this_var_num_card_map;
					this_var_num_card_map.insert(make_pair(now_plan_nodes, card_estimation));
					card_cache.push_back(this_var_num_card_map);

					map<vector<int>, vector<vector<unsigned>>> this_var_num_sample_map;
					this_var_num_sample_map.insert(make_pair(now_plan_nodes, this_sample));
					sample_cache.push_back(this_var_num_sample_map);

				} else {
					card_cache[now_plan_node_num - 2].insert(make_pair(now_plan_nodes, card_estimation));
					sample_cache[now_plan_node_num - 2].insert(make_pair(now_plan_nodes, this_sample));
				}

				vector<int> linked_nei_id;
				for (int i = 0; i < basicquery->getVarDegree(next_join_node); ++i) {
					if (find(last_plan_nodes.begin(), last_plan_nodes.end(), basicquery->getEdgeNeighborID(next_join_node, i))
					!= last_plan_nodes.end()) {
						linked_nei_id.push_back(basicquery->getEdgeNeighborID(next_join_node, i));
					}
				}

				unsigned multiple = 1+s_o_list_average_size[linked_nei_id[0]][next_join_node];
				for(auto x : linked_nei_id){
					multiple = min(multiple, s_o_list_average_size[x][next_join_node]+1);
					//					multiple += s_o_list_average_size[x][next_join_node];

				}
				return card_cache[now_plan_node_num - 3][last_plan_nodes]*multiple;

			} else{

				vector<int> linked_nei_id;
				for (int i = 0; i < basicquery->getVarDegree(next_join_node); ++i) {
					if (find(last_plan_nodes.begin(), last_plan_nodes.end(), basicquery->getEdgeNeighborID(next_join_node, i))
					!= last_plan_nodes.end()) {
						linked_nei_id.push_back(basicquery->getEdgeNeighborID(next_join_node, i));
					}
				}
				unsigned multiple = 1+s_o_list_average_size[linked_nei_id[0]][next_join_node];
				for(auto x : linked_nei_id){
					multiple = min(multiple, s_o_list_average_size[x][next_join_node]+1);
				}

				return card_cache[now_plan_node_num - 3][last_plan_nodes]*multiple;//+var_to_num_map[next_join_node];
			}
		}

	}
}

unsigned long PlanGenerator::get_card(const vector<int> &nodes){
	return card_cache[nodes.size()-2][nodes];
}

unsigned long PlanGenerator::cost_model_for_wco(PlanTree* last_plan,
											const vector<int> &last_plan_node, int next_node, const vector<int> &now_plan_node){

	return last_plan->plan_cost + card_estimator(last_plan_node, next_node, now_plan_node);
}

unsigned long PlanGenerator::cost_model_for_binary(const vector<int> &plan_a_nodes, const vector<int> &plan_b_nodes,
											   PlanTree* plan_a, PlanTree* plan_b){

	unsigned long plan_a_card = get_card(plan_a_nodes);
	unsigned long plan_b_card = get_card(plan_b_nodes);
	unsigned long min_card = min(plan_a_card, plan_b_card);
	unsigned long max_card = max(plan_a_card, plan_b_card);

	return 2*min_card + max_card + plan_a->plan_cost + plan_b->plan_cost;
}

//    save every nei_id of now_in_plan_node in nei_node
void PlanGenerator::get_nei_by_subplan_nodes(const vector<int> &need_join_nodes,
										 const vector<int> &last_plan_node, set<int> &nei_node){
	;
	for(int node_in_plan : last_plan_node){
		for(int i = 0; i < basicquery->getVarDegree(node_in_plan); ++i){
			if(find(last_plan_node.begin(), last_plan_node.end(), basicquery->getEdgeNeighborID(node_in_plan, i))
			== last_plan_node.end() && basicquery->getEdgeNeighborID(node_in_plan, i) != -1 &&
			find(need_join_nodes.begin(), need_join_nodes.end(), basicquery->getEdgeNeighborID(node_in_plan, i))
			!= need_join_nodes.end()){
				nei_node.insert(basicquery->getEdgeNeighborID(node_in_plan, i));
			}
		}
	}
}

void PlanGenerator::get_join_nodes(const vector<int> &plan_a_nodes, vector<int> &other_nodes, set<int> &join_nodes){
	for(int node : other_nodes){
		for(int i = 0; i < basicquery->getVarDegree(node); ++i){
			if(find(plan_a_nodes.begin(), plan_a_nodes.end(), basicquery->getEdgeNeighborID(node, i))!= plan_a_nodes.end()){
				join_nodes.insert(basicquery->getEdgeNeighborID(node, i));
			}
		}
	}
}

// first node
void PlanGenerator::considerallscan(vector<int> &need_join_nodes, bool use_sample){

	//special case: there is no query_var linked with constant, than need estimate by p2s or p2o
	bool is_special = true;
	random_device rd;
	mt19937 eng(rd());

	for(int i = 0 ; i < basicquery->getVarNum(); ++i) {

		if (basicquery->if_need_retrieve(i)) {

			need_join_nodes.push_back(i);
			vector<int> this_node{i};
			PlanTree *new_scan = new PlanTree(i);


			var_to_num_map[i] = (*id_caches)[i]->size();
			new_scan->plan_cost = var_to_num_map[i];

			list<PlanTree *> this_node_plan;
			this_node_plan.push_back(new_scan);

			if (plan_cache.size() < 1) {
				map<vector<int>, list<PlanTree *>> one_node_plan_map;

				one_node_plan_map.insert(make_pair(this_node, this_node_plan));

				plan_cache.push_back(one_node_plan_map);
			} else {
				plan_cache[0].insert(make_pair(this_node, this_node_plan));
			}

			if (use_sample) {

				auto list_size = (*id_caches)[i]->size();
				// IDList* need_insert_vec;
				vector<unsigned> need_insert_vec;
				if (list_size <= SAMPLE_CACHE_MAX) {
					// need_insert_vec = new IDList((*id_caches)[i]->getList());
					need_insert_vec.assign((*id_caches)[i]->getList()->begin(), (*id_caches)[i]->getList()->end());
				} else {
					unsigned sample_size = get_sample_size(list_size);
					// need_insert_vec = new IDList(sample_size);
					need_insert_vec.reserve(sample_size);

					auto id_cache_list = (*id_caches)[i]->getList();

					uniform_int_distribution<unsigned> dis(0, list_size);
					for (unsigned sample_num = 0; sample_num < sample_size; ++sample_num) {
						unsigned index_need_insert = dis(eng);
						// need_insert_vec->addID((*id_cache_list)[index_need_insert]);
						need_insert_vec.push_back((*id_cache_list)[index_need_insert]);
					}
				}
				var_to_sample_cache[i] = std::move(need_insert_vec);

			}else {

				cout << "degree: " << basicquery->getVarDegree(i) << endl;
				for (int j = 0; j < basicquery->getVarDegree(i); ++j) {
					if (basicquery->getEdgePreID(i, j) == statistics->type_pre_id) {
						int triple_id = basicquery->getEdgeID(i, j);
						string type_name = basicquery->getTriple(triple_id).object;
						var_to_type_map[i] = kvstore->getIDByEntity(type_name);

					}
				}
			}
		}
	}

}

// add one node, the added node need to be linked by nodes in plan before
void PlanGenerator::considerwcojoin(unsigned node_num, const vector<int> &need_join_nodes){
	auto plan_tree_list = plan_cache[node_num - 2];
	for(const auto &last_node_plan : plan_tree_list){
		set<int> nei_node;

		get_nei_by_subplan_nodes(need_join_nodes, last_node_plan.first, nei_node);

		PlanTree* last_best_plan = get_best_plan(last_node_plan.first);

		for(int next_node : nei_node) {

			vector<int> new_node_vec(last_node_plan.first);
			new_node_vec.push_back(next_node);
			sort(new_node_vec.begin(), new_node_vec.end());


			PlanTree* new_plan = new PlanTree(last_best_plan, next_node);
			unsigned long cost = cost_model_for_wco(last_best_plan, last_node_plan.first,
													next_node, new_node_vec);
			new_plan->plan_cost = cost;

			if(plan_cache.size() < node_num){
				map<vector<int>, list<PlanTree*>> this_num_node_plan;
				list<PlanTree*> this_nodes_plan;
				this_nodes_plan.push_back(new_plan);
				this_num_node_plan.insert(make_pair(new_node_vec, this_nodes_plan));
				plan_cache.push_back(this_num_node_plan);

			} else{
				if(plan_cache[node_num-1].find(new_node_vec) == plan_cache[node_num-1].end()){
					list<PlanTree*> this_nodes_plan;
					this_nodes_plan.push_back(new_plan);
					plan_cache[node_num-1].insert(make_pair(new_node_vec, this_nodes_plan));
				}else{
					plan_cache[node_num-1][new_node_vec].push_back(new_plan);
				}
			}

		}
	}

}

// not add nodes, but to consider if binaryjoin could decrease cost
void PlanGenerator::considerbinaryjoin(unsigned node_num){

	unsigned min_size = 3;
	unsigned max_size = min(min_size, node_num - 2);


	for(const auto &need_considerbinaryjoin_nodes_plan : plan_cache[node_num - 1]){

		unsigned long last_plan_smallest_cost = get_best_plan(need_considerbinaryjoin_nodes_plan.first)->plan_cost;

		for(unsigned small_plan_nodes_num = min_size; small_plan_nodes_num <= max_size; ++ small_plan_nodes_num){
			for(const auto &small_nodes_plan : plan_cache[small_plan_nodes_num-1]){
				if(OrderedVector::contain_sub_vec(need_considerbinaryjoin_nodes_plan.first, small_nodes_plan.first)) {
					vector<int> other_nodes;
					OrderedVector::subtract(need_considerbinaryjoin_nodes_plan.first, small_nodes_plan.first,other_nodes);
					set<int> join_nodes;
					get_join_nodes(small_nodes_plan.first, other_nodes, join_nodes);

					if (join_nodes.size() >= 1 && join_nodes.size() + other_nodes.size() < node_num - 1) {

						OrderedVector::vec_set_union(other_nodes, join_nodes);

						if (plan_cache[other_nodes.size() - 1].find(other_nodes) !=
						plan_cache[other_nodes.size() - 1].end()) {

							PlanTree *small_best_plan = get_best_plan(small_nodes_plan.first);
							PlanTree *another_small_best_plan = get_best_plan(other_nodes);

							unsigned long now_cost = cost_model_for_binary(small_nodes_plan.first,
																		   other_nodes, small_best_plan,
																		   another_small_best_plan);

							if (now_cost < last_plan_smallest_cost) {
								//                            build new plan and add to plan_cache
								PlanTree *new_plan = new PlanTree(small_best_plan, another_small_best_plan);
								new_plan->plan_cost = now_cost;
								plan_cache[node_num - 1][need_considerbinaryjoin_nodes_plan.first].push_back(new_plan);
								last_plan_smallest_cost = now_cost;

							}
						}
					}
				}

			}
		}
	}
}


//  enumerate all execution plan, and build cost_cache
int PlanGenerator::enum_query_plan(vector<int> &need_join_nodes){



	long t1 = Util::get_cur_time();
	considerallscan(need_join_nodes);
	long t2 = Util::get_cur_time();
	cout << "consider all scan, used " << (t2 - t1) << "ms." << endl;
	for(auto x : var_to_num_map){
		cout << "var: " << basicquery->getVarName(x.first) << ", num: " << x.second << endl;
	}
	for(auto x : var_to_sample_cache){
		cout << "var: " << basicquery->getVarName(x.first) << ", sample num: " << x.second.size() << endl;
	}




	for(unsigned i = 1; i < need_join_nodes.size(); ++i){
		long t3 = Util::get_cur_time();
		considerwcojoin(i+1, need_join_nodes);
		long t4 = Util::get_cur_time();
		cout << "i = " << i << ", considerwcojoin, used " << (t4 - t3) << "ms." << endl;

		if(i >= 4){
			//            begin when nodes_num >= 5
			considerbinaryjoin(i+1);
			//      cout << "binary join consider here" << endl;
		}
	}

	return need_join_nodes.size();
}

PlanTree* PlanGenerator::get_best_plan(const vector<int> &nodes){

	PlanTree* best_plan = nullptr;
	unsigned long min_cost = ULONG_MAX;


	for(const auto &plan : plan_cache[nodes.size()-1][nodes]){
		if(plan->plan_cost < min_cost){
			best_plan = plan;
			min_cost = plan->plan_cost;
		}
	}

	return best_plan;
}

// only used when get best plan for all nodes in basicquery
PlanTree* PlanGenerator::get_best_plan_by_num(int total_var_num){

	PlanTree* best_plan = nullptr;
	unsigned long min_cost = ULONG_MAX	;


	//	for(int i =0;i < plan_cache.size();++i){
	//		for(auto x:plan_cache[i]){
	//			for(int j = 0;j<x.first.size();++j){
	//				cout << x.first[j]<< " ";
	//			}
	//			cout <<endl;
	//			for(auto y:x.second){
	//				y->print(basicquery);
	//			}
	//		}
	//	}

	int count = 0;
	for(const auto &nodes_plan : plan_cache[total_var_num-1]){
		for(const auto &plan_tree : nodes_plan.second){
			//    	plan_tree->print(basicquery);
			count ++;
			if(plan_tree->plan_cost < min_cost){
				best_plan = plan_tree;
				min_cost = plan_tree->plan_cost;
			}
		}
	}
	cout << "during enumerate plans, get " << count << " possible best plans." << endl;
	return best_plan;
}


PlanTree* PlanGenerator::get_normal_plan() {


	vector<int> need_join_nodes;

	int join_nodes_num = enum_query_plan(need_join_nodes);

	PlanTree* best_plan = get_best_plan_by_num(join_nodes_num);

	for(int i = 0; i < basicquery->getVarNum(); ++i){
		if(find(need_join_nodes.begin(), need_join_nodes.end(), i) == need_join_nodes.end()
		&& !basicquery->isSatelliteInJoin(i)){
			best_plan = new PlanTree(best_plan, i);
		}
	}

	//  selected, and not need_retrieve
	for(int i = 0; i < basicquery->getSelectVarNum(); ++i){
		if(!basicquery->if_need_retrieve(i)){
			best_plan = new PlanTree(best_plan, i);
		}
	}

	return best_plan;
}


PlanTree *PlanGenerator::get_special_no_pre_var_plan() {
	if((*id_caches)[0]->size() <= (*id_caches)[1]->size())
		return new PlanTree(vector<int> {0,1});
	else
		return new PlanTree(vector<int> {1,0});
}

PlanTree *PlanGenerator::get_special_one_triple_plan() {

	;
}