//////////////////////////////////////////////////////////////////////////////////
#include "tools.hpp"
#include "CSPL.hpp"
#include "CDPL.hpp"
#include "CTPL.hpp"
#include "queryPlan_generator.hpp"
#include "WeightUpdateTask.hpp"
#include <sys/syscall.h>

//////////////////////////////////////////////////////////////////////////////////////////////////
void metaDataHandler_task(int id, int threadPool_level, WeightUpdateTask* task, 
						  Memory* memory, int currentNode) {
	
	/////////////////////////
/*	if(task == NULL)
		return;
	
	/////////////////////////
	Database* db = dbPool->get(id + _workerThread_index_per_level_[threadPool_level]);
	
	if (task->isUpdateEdge()) {
		printf("begin update edgeweight\n");
		vector<triple_t> edges = task->getEdge();
		if (edges.size() > 0)
			db->batch_up_edgeweight(currentNode, edges);
		printf("end update edgeweight \n");
	} else {
		printf("begin update vertex weight\n");
		vector<sid_t> vs = task->getVertex();
		int incr_weight = task->getIncrWeight();
		if(vs.size() > 0) {
			db->batch_up_vertexweight(currentNode, vs, incr_weight);
			memory->reassign_queue->push(currentNode, vs);
		}
		printf("end update vertex weight\n");
	}
	
	/////////////////////////
	delete task;*/
}

//////////////////////////////////////////////////////////////////////////////////
// This server waits to receive <sub-query plans>.
void metaDataHandler_proxy(int currentNode, HeliosConfig* config, Memory* memory) { 	
         printf("metaDataHandler_proxy thread lwpid = %u currentNode=%d\n", syscall(SYS_gettid),currentNode); 
	/////////////////////////
	// Third pool of worker threads, whereby the evaluation task of each <sub-query plan> is assigned to
	// an individual worker.
	// Third pool has a level equal to 2.
	ctpl::thread_pool Thread_Pool(config->_workerThread_nbr_per_level_[3], 3, currentNode, config
								  , memory);	
  
	/////////////////////////
	std::cout << std::endl;
	std::cout << "The metaDataHandler proxy started successfully." << std::endl;
	 
  	/////////////////////////
	// Wait for recieving <sub-query plans> from a <(sub-)query plan> task, 
	// and assign the evaluation task of each <sub-query plan> to an individual worker.
	while (true) {  	
		
		WeightUpdateTask* task = memory->reassign_queue->pop_weight_task(currentNode);
			
  	    /////////////////////////
	    if(task != NULL)
		Thread_Pool.push(metaDataHandler_task, 3, task, memory, currentNode);	
	    else 
	    	boost::this_thread::sleep( boost::posix_time::milliseconds(10) );

    }  
} 

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// <queryPlan_task_global> evaluates the received query plan <qp>.
// Note that <whoCalledMe> can be either "false" or "true" which means <qp> was sent by either <queryPlan_proxy> or 
// <subQueryPlan_proxy> respectively. So, there are two cases:

// 1- <qp> was sent by <queryPlan_proxy>; so according to the number of instances of <sNode>, several independent 
// query plans (each one rooted with one of the instances) are created sequentially and sent to the 
// <subQueryPlan_proxy> of the existing server.
// Note that in this case, we do not do any pruning stuffs on the received results of each query plan and
// the empty results are sent to the corresponding <query_task>.

// 2- <qp> was sent by <subQueryPlan_proxy>; so it is evaluated and pruning (step 1&2) are applied on the results.
// They are stored in the corresponding place in <qp.root.result_map>.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void queryPlan_task_global(int id, int threadPool_level, queryPlan qp, int currentNode, 
						   HeliosConfig* config, Memory* memory, bool whoCalledMe) {		  						
		
	/////////////////////////////
	QueryNode *sNode = qp.root;
	
	/////////////////////////////
	zmq::socket_t* subQueryPlan_push_socket;
	string current_nodeName = config->host_names[(int) currentNode];
	subQueryPlan_push_socket = sPool->takeSocket(id + _workerThread_index_per_level_[threadPool_level],
												 0, "tcp://" + current_nodeName + ':' + subQueryPlan_proxy_port);
												 
	/////////////////////////////
	zmq::socket_t* subQueryPlan_result_pull_socket; 
	subQueryPlan_result_pull_socket = sPool->takeSocket(id + _workerThread_index_per_level_[threadPool_level]);
	
	/////////////////////////////
	zmq::message_t subQueryPlanResult;  
	
	/////////////////////////////
	queryPlan sub_qp;
	
	sub_qp.qpr.result_map.resize(qp.nodeList.size());
	sub_qp.qpr.binding_map.resize(qp.nodeList.size());
	sub_qp.qpr.pruned_step1List.resize(qp.nodeList.size());
	sub_qp.qpr.pruned_step2List.resize(qp.nodeList.size());
	sub_qp.qpr.subListOrObjList_Set = qp.qpr.subListOrObjList_Set;
	sub_qp.nodeList = qp.nodeList;
	sub_qp.sender_ip = current_nodeName;
	sub_qp.sender_port = sPool->takePortNo(id + _workerThread_index_per_level_[threadPool_level]);
	
	/////////////////////////////
	queryPlan_result sub_qpResult;
	
	/////////////////////////////
	std::vector<int> joint_children_index;
	joint_children_index.clear();
	
	/////////////////////////////
	// Determine the index of <sNode>'s result in <qp.result_map> and store it in
	// <sNode_result_index>.
	int sNode_result_index;	
	
	/////////////////////////////
	if (!whoCalledMe) {	
	
		/////////////////////////////
		vector<sid_t> src_bind;
		dbPool->get(id + _workerThread_index_per_level_[threadPool_level])->evaluate_root_binding_local
		                                                                    (sNode, &src_bind);			
		/////////////////////////////
		sub_qp.root = sNode;
		sNode->diable_read_index();
				
		for (int i = 0; i < src_bind.size(); i++) {
										
			/////////////////////////////
			sub_qp.root->bind_val.clear();
			sub_qp.root->bind_val.push_back(src_bind[i]);
			sub_qp.root->root_vertex = src_bind[i];
			
			/////////////////////////////
			string rstr = wrap_queryPlan(sub_qp);
			zmq::message_t subQueryPlan(rstr.length());
			memcpy((void *)subQueryPlan.data(), rstr.c_str(), rstr.length());
			send_(*subQueryPlan_push_socket, &subQueryPlan);

			/////////////////////////////
			receive_(*subQueryPlan_result_pull_socket, &subQueryPlanResult);
			sub_qpResult = unwrap_queryPlan_result(std::string((char *)subQueryPlanResult.data(), 
												   subQueryPlanResult.size()));

			/////////////////////////////
			// for (int i = 0; i < qp.nodeList.size(); i++) {
			
				// if (sub_qpResult.pruned_step1List[i] == true) qp.qpr.pruned_step1List[i] = true;
				// if (sub_qpResult.pruned_step2List[i] == true) qp.qpr.pruned_step2List[i] = true;
			
				// if (sub_qpResult.result_map[i].size() > 0)
					// qp.qpr.result_map[i].insert(qp.qpr.result_map[i].end(), 
												// sub_qpResult.result_map[i].begin(),
												// sub_qpResult.result_map[i].end());

				// if (sub_qpResult.binding_map[i].size() > 0) 
					// qp.qpr.binding_map[i].insert(qp.qpr.binding_map[i].end(), 
												 // sub_qpResult.binding_map[i].begin(),
												 // sub_qpResult.binding_map[i].end());	
			// }													   
		}
	}
	else {
	
		/////////////////////////////
		for (int i = 0; i < qp.nodeList.size(); i++) {
			qp.qpr.pruned_step1List[i] = false;
			qp.qpr.pruned_step2List[i] = false;
		}
				
		/////////////////////////////
		// Evaluate all triple patterns whose source is <sNode>, and return the corresponding 
		// RDF instances in <sNode_result>.
		vector<triple_t> sNode_result;
		sNode_result.clear();
			
		/////////////////////////////
		long test_time = get_usec();
		dbPool->get(id + _workerThread_index_per_level_[threadPool_level])->
					client_pattern_evaluate_global(sNode, &sNode_result);
					
		/////////////////////////////
		std::cout << "currentNode = " << current_nodeName << " client_pattern_evaluate_time = " 
				  << std::to_string(((float) (get_usec() - test_time))/1000000) << std::endl;	
						
		for (int i = 0; i < qp.nodeList.size(); i++)
			if (qp.nodeList[i] == sNode) {
				qp.qpr.result_map[i] = sNode_result;
				sNode_result_index = i;
				break;
			}	
																																	
		///////////////// Information before prune-step1
		cout << std::endl << BOLDRED << "Before pruning-step1 of " << "<"
			 << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " 
			 << "<" << current_nodeName << ">" << std::endl 
			 << "1) The binding size: " << sNode->bind_val.size() << std::endl 
			 << "2) The RDF result size: " << qp.qpr.result_map[sNode_result_index].size() << std::endl
			 << "3) The bind_to_prune size: " << sNode->bind_to_prune.size() << " currentNode = " 
			 << current_nodeName << " from where = " << qp.sender_ip << std::endl << RESET;
									
		/////////////////
		// Apply The first step of pruning on <sNode>
		qp.prune_step1(sNode, sNode_result_index, current_nodeName);
														
		///////////////// Information After prune-step1
		if (qp.qpr.pruned_step1List[sNode_result_index]) {
			cout << std::endl << BOLDGREEN << "After pruning-step1 of " << "<"
				 << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " 
				 << "<" << current_nodeName << ">" << std::endl 
				 << "currentNode = " << current_nodeName <<"1) The binding size: " 
				 << qp.qpr.binding_map[sNode_result_index].size() << std::endl 
				 << "2) The RDF result size: " << qp.qpr.result_map[sNode_result_index].size() 
				 << " from where = " << qp.sender_ip<< std::endl << RESET;			
		}
		else 				
			cout << std::endl << BOLDWHITE << " *** No pruning-step1 of "
				 << "<" << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << "> at <" 
				 << current_nodeName << ">" << " ***"  <<"currentNode=" << current_nodeName<< " from where = " 
				 << qp.sender_ip << RESET << std::endl;
																						
	
		if (qp.qpr.binding_map[sNode_result_index].size() != 0) {
				
			/////////////////
			QueryNode *tNode;
												
			/////////////////////////
			int expected_nbr_of_results = 0;
		
			/////////////////////////
			for (int e = 0; e < sNode->edges.size(); e++) {
							
				/////////////////////////
				tNode = sNode->edges[e]->node;
												
				if (tNode->edges.size() > 0) // tNode is a joint (non-leaf) node
				{
																			   if(tNode->bind_val.size() == 0)												printf("tnode bind_val is 0\n");
					/////////////////////////
					for (int i = 0; i < qp.nodeList.size(); i++)
						if (qp.nodeList[i] == tNode) {
							joint_children_index.push_back(i);;
							break;
						}	
									
					/////////////////////////////
					expected_nbr_of_results++;
										
					/////////////////////////////
					sub_qp.root = tNode;
					sub_qp.root->root_vertex = sNode->root_vertex;
											
					/////////////////////////////
					string rstr = wrap_queryPlan(sub_qp);
					zmq::message_t subQueryPlan(rstr.length());
					memcpy((void *)subQueryPlan.data(), rstr.c_str(), rstr.length());
					send_(*subQueryPlan_push_socket, &subQueryPlan);
				}		
			}
		
			for (int result_nbr = 0; result_nbr != expected_nbr_of_results; result_nbr++) {
		
				receive_(*subQueryPlan_result_pull_socket, &subQueryPlanResult);
						queryPlan_result sub_qpResult = unwrap_queryPlan_result
														(std::string((char *)subQueryPlanResult.data(), 
														subQueryPlanResult.size()));									
				/////////////////////////////
				for (int i = 0; i < qp.nodeList.size(); i++) {
			
					if (sub_qpResult.pruned_step1List[i] == true) qp.qpr.pruned_step1List[i] = true;
					if (sub_qpResult.pruned_step2List[i] == true) qp.qpr.pruned_step2List[i] = true;
			
					if (sub_qpResult.result_map[i].size() > 0)
						qp.qpr.result_map[i].insert(qp.qpr.result_map[i].end(), 
													sub_qpResult.result_map[i].begin(),
													sub_qpResult.result_map[i].end());

					if (sub_qpResult.binding_map[i].size() > 0) 
						qp.qpr.binding_map[i].insert(qp.qpr.binding_map[i].end(), 
												sub_qpResult.binding_map[i].begin(),
												sub_qpResult.binding_map[i].end());	
				}		
			}	
													  
			for (int i = 0; i < joint_children_index.size(); i++)
				if (qp.qpr.result_map[joint_children_index[i]].size() == 0)
					for (int j = 0; j < qp.qpr.result_map.size(); j++) {
						qp.qpr.result_map[j].clear();
						qp.qpr.binding_map[j].clear();
					}
					
			/////////////////
			// Apply The second step of pruning on <sNode>
			qp.prune_step2(sNode, sNode_result_index, current_nodeName);
	
			/////////////////
			if (qp.qpr.result_map[sNode_result_index].size() == 0)
				for (int j = 0; j < qp.qpr.result_map.size(); j++) {
					qp.qpr.result_map[j].clear();
					qp.qpr.binding_map[j].clear();
				}	
		}
	}
			
	//////////////////////////////
	// Step3: send the result of <qp> to the corresponding <query plan task> or <query task>. 
			 
	string endpoint = "tcp://" + qp.sender_ip + ":" + qp.sender_port;
  
	zmq::socket_t *queryPlan_result_push_socket; 
	queryPlan_result_push_socket = sPool->takeSocket(id + _workerThread_index_per_level_[threadPool_level],
													 0, endpoint);
   
	/////////////////////////////
	if (!whoCalledMe) {
		qp.qpr.result_map.resize(0);
		qp.qpr.binding_map.resize(0);
		qp.qpr.pruned_step1List.resize(0);
		qp.qpr.pruned_step2List.resize(0);
		std::cout <<"!whocalledMe" << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " << current_nodeName << ">"
			<< "from where=" << qp.sender_ip<< std::endl ;
	}
	
	string rstr = wrap_queryPlan_result(qp.qpr);
	zmq::message_t queryPlanResult(rstr.length());
	memcpy((void *) queryPlanResult.data(), rstr.c_str(), rstr.length());
	send_(*queryPlan_result_push_socket, &queryPlanResult);
			
	if (whoCalledMe) {
		///////////////// Information after prune-step2
		if (qp.qpr.pruned_step2List[sNode_result_index]) {
			cout << std::endl << BOLDGREEN << "After pruning-step2 of " << "<"
				<< qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " << "<"
				<< current_nodeName << ">" << std::endl 
				<<"currentNode=" << current_nodeName << "1) The binding size: " << qp.qpr.binding_map[sNode_result_index].size() << std::endl 
				<< "2) The RDF result size: " << qp.qpr.result_map[sNode_result_index].size() 
				<< "from where=" << qp.sender_ip<< std::endl << RESET;
		}
		else 				
			cout << std::endl << BOLDWHITE << " *** No pruning-step2 of "
				 << "<" << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << "> at <" 
				 << current_nodeName << ">" << " ***"  << "currentNode=" << current_nodeName<< "from where=" << qp.sender_ip <<RESET << std::endl;
						
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// <queryPlan_task_global> evaluates the assigned query plan <qp>.
// <whoCalledMe> is "false" or "true" which shows <qp> has been sent by either <queryPlan_proxy> or 
// <subQueryPlan_proxy> respectively.
// If <qp> is sent by <queryPlan_proxy> we do not do any pruning stuffs on the results and send empty results
// to the corresponding <query_task>.
// According to the number of instances of <sNode>, It creates several independent query plans and send them to
// the <subQueryPlan_proxy> of the existing server.
// It generates a set of triple instances and applies pruning (step 1&2) on them.
// They are stored in the corresponding place in <qp.root.result_map>.
// Also the binding of <sNode> is determined and stored in the corresponding place in <binding_map>.
// <evaluate> returns false if the evaluation of a joint child of <sNode> returns no result. This means 
// that the evaluation of <sNode> in turn should return no result.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void queryPlan_task_global_back(int id, int threadPool_level, queryPlan qp, int currentNode, 
						   HeliosConfig* config, Memory* memory, bool whoCalledMe) {		  						
		
	/////////////////////////////
	QueryNode *sNode = qp.root;
	
	/////////////////////////////
	zmq::socket_t* subQueryPlan_push_socket;
	string current_nodeName = config->host_names[(int) currentNode];
	subQueryPlan_push_socket = sPool->takeSocket(id + _workerThread_index_per_level_[threadPool_level],
												 0, "tcp://" + current_nodeName + ':' + subQueryPlan_proxy_port);
	/////////////////////////////
	queryPlan sub_qp;
	
	sub_qp.qpr.result_map.resize(qp.nodeList.size());
	sub_qp.qpr.binding_map.resize(qp.nodeList.size());
	sub_qp.qpr.pruned_step1List.resize(qp.nodeList.size());
	sub_qp.qpr.pruned_step2List.resize(qp.nodeList.size());
	sub_qp.qpr.subListOrObjList_Set = qp.qpr.subListOrObjList_Set;
	sub_qp.nodeList = qp.nodeList;
	sub_qp.sender_ip = current_nodeName;
	sub_qp.sender_port = sPool->takePortNo(id + _workerThread_index_per_level_[threadPool_level]);
	
	/////////////////////////
	int expected_nbr_of_results = 0;
	
	/////////////////////////////
	std::vector<int> joint_children_index;
	joint_children_index.clear();
	
	/////////////////////////////
	// Determine the index of <sNode>'s result in <qp.result_map> and store it in
	// <sNode_result_index>.
	int sNode_result_index;	
	
	/////////////////////////////
	if (!whoCalledMe) {	
	
		/////////////////////////////
		vector<sid_t> src_bind;
		dbPool->get(id + _workerThread_index_per_level_[threadPool_level])->evaluate_root_binding_local
		                                                                    (sNode, &src_bind);			
		/////////////////////////////
		sub_qp.root = sNode;
		sNode->diable_read_index();
				
		for (int i = 0; i < src_bind.size(); i++) {
										
			/////////////////////////////
			sub_qp.root->bind_val.clear();
			sub_qp.root->bind_val.push_back(src_bind[i]);
			sub_qp.root->root_vertex = src_bind[i];
			
			/////////////////////////////
			string rstr = wrap_queryPlan(sub_qp);
			zmq::message_t subQueryPlan(rstr.length());
			memcpy((void *)subQueryPlan.data(), rstr.c_str(), rstr.length());
			send_(*subQueryPlan_push_socket, &subQueryPlan);		
		}
		expected_nbr_of_results = src_bind.size();
	}
	else {
	
		/////////////////////////////
		for (int i = 0; i < qp.nodeList.size(); i++) {
			qp.qpr.pruned_step1List[i] = false;
			qp.qpr.pruned_step2List[i] = false;
		}
				
		/////////////////////////////
		// Evaluate all triple patterns whose source is <sNode>, and return the corresponding 
		// RDF instances in <sNode_result>.
		vector<triple_t> sNode_result;
		sNode_result.clear();
			
		/////////////////////////////
		long test_time = get_usec();
		dbPool->get(id + _workerThread_index_per_level_[threadPool_level])->
					client_pattern_evaluate_global(sNode, &sNode_result);
					
		/////////////////////////////
		std::cout << "currentNode = " << current_nodeName << " client_pattern_evaluate_time = " 
				  << std::to_string(((float) (get_usec() - test_time))/1000000) << std::endl;	
						
		for (int i = 0; i < qp.nodeList.size(); i++)
			if (qp.nodeList[i] == sNode) {
				qp.qpr.result_map[i] = sNode_result;
				sNode_result_index = i;
				break;
			}	
																																	
		///////////////// Information before prune-step1
		cout << std::endl << BOLDRED << "Before pruning-step1 of " << "<"
			 << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " 
			 << "<" << current_nodeName << ">" << std::endl 
			 << "1) The binding size: " << sNode->bind_val.size() << std::endl 
			 << "2) The RDF result size: " << qp.qpr.result_map[sNode_result_index].size() << std::endl
			 << "3) The bind_to_prune size: " << sNode->bind_to_prune.size() << " currentNode = " 
			 << current_nodeName << " from where = " << qp.sender_ip << std::endl << RESET;
									
		/////////////////
		// Apply The first step of pruning on <sNode>
		qp.prune_step1(sNode, sNode_result_index, current_nodeName);
												
		///////////////// Information After prune-step1
		if (qp.qpr.pruned_step1List[sNode_result_index]) {
			cout << std::endl << BOLDGREEN << "After pruning-step1 of " << "<"
				 << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " 
				 << "<" << current_nodeName << ">" << std::endl 
				 << "currentNode = " << current_nodeName <<"1) The binding size: " 
				 << qp.qpr.binding_map[sNode_result_index].size() << std::endl 
				 << "2) The RDF result size: " << qp.qpr.result_map[sNode_result_index].size() 
				 << " from where = " << qp.sender_ip<< std::endl << RESET;			
		}
		else 				
			cout << std::endl << BOLDWHITE << " *** No pruning-step1 of "
				 << "<" << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << "> at <" 
				 << current_nodeName << ">" << " ***"  <<"currentNode=" << current_nodeName<< " from where = " 
				 << qp.sender_ip << RESET << std::endl;
																						
		/////////////////
		QueryNode *tNode;
			
		/////////////////////////////
		// Step1: send the <subQuery plans> of <qp> to the <queryPlan_tasks> of the current server.
								
		/////////////////////////
		for (int e = 0; e < sNode->edges.size(); e++) {
							
			/////////////////////////
			tNode = sNode->edges[e]->node;
												
			if (tNode->edges.size() > 0) // tNode is a joint (non-leaf) node
			{
																													
				/////////////////////////
				for (int i = 0; i < qp.nodeList.size(); i++)
					if (qp.nodeList[i] == tNode) {
						joint_children_index.push_back(i);;
						break;
					}	
									
					/////////////////////////////
					expected_nbr_of_results++;
										
					/////////////////////////////
					sub_qp.root = tNode;
					sub_qp.root->root_vertex = sNode->root_vertex;
											
					/////////////////////////////
					string rstr = wrap_queryPlan(sub_qp);
					zmq::message_t subQueryPlan(rstr.length());
					memcpy((void *)subQueryPlan.data(), rstr.c_str(), rstr.length());
					send_(*subQueryPlan_push_socket, &subQueryPlan);
			}		
		}
	}
			
	/////////////////////////////
	// Step2: receive the results of the sent <subQuery plans> from their corresponding <queryPlan_tasks>.
	zmq::socket_t *subQueryPlan_result_pull_socket; 
	subQueryPlan_result_pull_socket = sPool->takeSocket(id + _workerThread_index_per_level_[threadPool_level]);
	
	for (int result_nbr = 0; result_nbr != expected_nbr_of_results; result_nbr++) {
		
		/////////////////////////////
		zmq::message_t subQueryPlanResult;  
		receive_(*subQueryPlan_result_pull_socket, &subQueryPlanResult);
				 queryPlan_result sub_qpResult = unwrap_queryPlan_result(std::string((char *)subQueryPlanResult.data(), 
																		 subQueryPlanResult.size()));									

		/////////////////////////////
		for (int i = 0; i < qp.nodeList.size(); i++) {
			
			if (sub_qpResult.pruned_step1List[i] == true) qp.qpr.pruned_step1List[i] = true;
			if (sub_qpResult.pruned_step2List[i] == true) qp.qpr.pruned_step2List[i] = true;
			
			if (sub_qpResult.result_map[i].size() > 0)
				qp.qpr.result_map[i].insert(qp.qpr.result_map[i].end(), 
											sub_qpResult.result_map[i].begin(),
											sub_qpResult.result_map[i].end());

			if (sub_qpResult.binding_map[i].size() > 0) 
				qp.qpr.binding_map[i].insert(qp.qpr.binding_map[i].end(), 
											 sub_qpResult.binding_map[i].begin(),
											 sub_qpResult.binding_map[i].end());	
		}		
	}	
													  
	/////////////////////////////
	if (whoCalledMe) {
		for (int i = 0; i < joint_children_index.size(); i++)
			if (qp.qpr.result_map[joint_children_index[i]].size() == 0)
				for (int j = 0; j < qp.qpr.result_map.size(); j++) {
					 qp.qpr.result_map[j].clear();
					 qp.qpr.binding_map[j].clear();
				}
					
		/////////////////
		// Apply The second step of pruning on <sNode>
		qp.prune_step2(sNode, sNode_result_index, current_nodeName);
	
		/////////////////
		if (qp.qpr.result_map[sNode_result_index].size() == 0)
			for (int j = 0; j < qp.qpr.result_map.size(); j++) {
				qp.qpr.result_map[j].clear();
				qp.qpr.binding_map[j].clear();
			}	
	}
			
	//////////////////////////////
	// Step3: send the result of <qp> to the corresponding <query plan task> or <query task>. 
			 
	string endpoint = "tcp://" + qp.sender_ip + ":" + qp.sender_port;
  
	zmq::socket_t *queryPlan_result_push_socket; 
	queryPlan_result_push_socket = sPool->takeSocket(id + _workerThread_index_per_level_[threadPool_level],
													 0, endpoint);
   
	/////////////////////////////
	if (!whoCalledMe) {
		qp.qpr.result_map.resize(0);
		qp.qpr.binding_map.resize(0);
		qp.qpr.pruned_step1List.resize(0);
		qp.qpr.pruned_step2List.resize(0);
		std::cout <<"!whocalledMe" << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " << current_nodeName << ">"
			<< "from where=" << qp.sender_ip<< std::endl ;
	}
	
	string rstr = wrap_queryPlan_result(qp.qpr);
	zmq::message_t queryPlanResult(rstr.length());
	memcpy((void *) queryPlanResult.data(), rstr.c_str(), rstr.length());
	send_(*queryPlan_result_push_socket, &queryPlanResult);
			
	if (whoCalledMe) {
		///////////////// Information after prune-step2
		if (qp.qpr.pruned_step2List[sNode_result_index]) {
			cout << std::endl << BOLDGREEN << "After pruning-step2 of " << "<"
				<< qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " << "<"
				<< current_nodeName << ">" << std::endl 
				<<"currentNode=" << current_nodeName << "1) The binding size: " << qp.qpr.binding_map[sNode_result_index].size() << std::endl 
				<< "2) The RDF result size: " << qp.qpr.result_map[sNode_result_index].size() 
				<< "from where=" << qp.sender_ip<< std::endl << RESET;
		}
		else 				
			cout << std::endl << BOLDWHITE << " *** No pruning-step2 of "
				 << "<" << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << "> at <" 
				 << current_nodeName << ">" << " ***"  << "currentNode=" << current_nodeName<< "from where=" << qp.sender_ip <<RESET << std::endl;
						
	}
}

/////////////////////////////////////////////////////////////////////////
// <queryPlan_task_local> evaluates the assigned query plan <qp>.
// It generates a set of triple instances and applies pruning (step 1&2) on them.
// They are stored in the corresponding place in <qp.root.result_map>.
// Also the binding of <sNode> is determined and stored in the corresponding place in <binding_map>.
// <evaluate> returns false if the evaluation of a joint child of <sNode> returns no result. This means 
// that the evaluation of <sNode> in turn should return no result.
// <whoCalledMe> "true" or "false" which determines whether <queryPlan_proxy> or <subQueryPlan_proxy> has called it.
// If it is called from <queryPlan_proxy> we do not do any pruning stuffs on the results and send empty results
// to the corresponding <query_task>.
/////////////////////////////////////////////////////////////////////////
void queryPlan_task_local(int id, int threadPool_level, queryPlan qp, int currentNode, 
						HeliosConfig* config, Memory* memory, bool whoCalledMe) {		  						
		
	///////////////
	for (int i = 0; i < qp.nodeList.size(); i++) {
		qp.qpr.pruned_step1List[i] = false;
		qp.qpr.pruned_step2List[i] = false;
	}
	
	///////////////
	string nodeName = config->host_names[(int) currentNode];
					
	///////////////	
	QueryNode *sNode = qp.root;
			
	/////////////////	
	// Evaluate all triple patterns whose source is <sNode>, and return the corresponding 
	// RDF instances in <sNode_result>.
	vector<triple_t> sNode_result;
	sNode_result.clear();
			
	/////////////////
	long test_time = get_usec();
	dbPool->get(id + _workerThread_index_per_level_[threadPool_level])->
			    client_pattern_evaluate_local(sNode, &sNode_result);
	std::cout <<"currentNode=" << currentNode <<"client_pattern_evaluate_time=" << std::to_string(((float) (get_usec() - test_time))/1000000) << std::endl;
	////////////////
	// Determine the index of <sNode>'s result in <qp.result_map> and store it in
	// <sNode_result_index>.
	int sNode_result_index;	
					
	////////////////
	for (int i = 0; i < qp.nodeList.size(); i++)
		if (qp.nodeList[i] == sNode) {
			qp.qpr.result_map[i] = sNode_result;
			sNode_result_index = i;
			break;
		}	
																																	
	///////////////// Information before prune-step1
	cout << std::endl << BOLDRED << "Before pruning-step1 of " << "<"
		 << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " 
		 << "<" << nodeName << ">" << std::endl 
		 << "1) The binding size: " << sNode->bind_val.size() << std::endl 
		 << "2) The RDF result size: " << qp.qpr.result_map[sNode_result_index].size() << std::endl
		 << "3) The bind_to_prune size: " << sNode->bind_to_prune.size() <<"currentNode=" << currentNode<< "from where=" << qp.sender_ip << std::endl << RESET;
									
	/////////////////
	// Apply The first step of pruning on <sNode>
	qp.prune_step1(sNode, sNode_result_index, nodeName);
												
	///////////////// Information After prune-step1
	if (qp.qpr.pruned_step1List[sNode_result_index]) {
		cout << std::endl << BOLDGREEN << "After pruning-step1 of " << "<"
			 << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " 
			 << "<" << nodeName << ">" << std::endl 
			 << "currentNode=" << currentNode <<"1) The binding size: " << qp.qpr.binding_map[sNode_result_index].size() << std::endl 
			 << "2) The RDF result size: " << qp.qpr.result_map[sNode_result_index].size() << "from where=" << qp.sender_ip<< std::endl << RESET;			
		}
	else 				
		cout << std::endl << BOLDWHITE << " *** No pruning-step1 of "
			 << "<" << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << "> at <" 
			 << nodeName << ">" << " ***"  <<"currentNode=" << currentNode<< "from where=" << qp.sender_ip << RESET << std::endl;
																						
	/////////////////
	QueryNode *tNode;
			
	/////////////////////////////
	std::vector<zmq::socket_t*> subQueryPlan_push_socket(10);
			
	/////////////////////////////
	// Step1: send the <subQuery plans> of <qp> to their corresponding <queryPlan_tasks>.
				
	/////////////////////////
	int nbr_of_results = 0;
	
	/////////////////////////
	std::vector<int> joint_children_index;
	joint_children_index.clear();
			
	/////////////////////////
	for (int e = 0; e < sNode->edges.size(); e++) {
							
		/////////////////
		tNode = sNode->edges[e]->node;
												
		if (tNode->edges.size() > 0) // tNode is a joint (non-leaf) node
		{
																													
			////////////////
			for (int i = 0; i < qp.nodeList.size(); i++)
				if (qp.nodeList[i] == tNode) {
					joint_children_index.push_back(i);;
					break;
				}	
			
			/////////////////////////
			map<int, vector<sid_t>> s_map;
			s_map = dbPool->get(id + _workerThread_index_per_level_[threadPool_level])
					->batch_get_global_vloc(tNode->bind_val);
					
			/////////////////////////
			std::vector<zmq::socket_t*> subQuery_push_socket(10);
										
			/////////////////////////////
			int nbr_of_subQueryPlans = 0;
					
			/////////////////////////////					
			for (std::map<int, vector<sid_t>>::iterator it = s_map.begin(); it != s_map.end(); it++) {
						
				/////////////////////////
				int server_nbr = it->first;
				string target_nodeName = config->host_names[server_nbr];
					
				/////////////////////////////
				subQueryPlan_push_socket[nbr_of_subQueryPlans] = 
				sPool->takeSocket(id + _workerThread_index_per_level_[threadPool_level],
								 nbr_of_subQueryPlans, "tcp://" + target_nodeName + 
								 ':' + subQueryPlan_proxy_port);
											
				/////////////////////////////
				queryPlan sub_qp;
						
				/////////////////////////////
				sub_qp.qpr.result_map.resize(qp.nodeList.size());
				sub_qp.qpr.binding_map.resize(qp.nodeList.size());
				sub_qp.qpr.pruned_step1List.resize(qp.nodeList.size());
				sub_qp.qpr.pruned_step2List.resize(qp.nodeList.size());
						
				/////////////////////////////
				sub_qp.qpr.subListOrObjList_Set = qp.qpr.subListOrObjList_Set;
				tNode->bind_val = it->second;
				sub_qp.root = tNode;
				sub_qp.nodeList = qp.nodeList;
				sub_qp.sender_ip = nodeName;
				sub_qp.sender_port = sPool->takePortNo(id + _workerThread_index_per_level_[threadPool_level]);
											
				string rstr = wrap_queryPlan(sub_qp);
				zmq::message_t subQueryPlan(rstr.length());
				memcpy((void *)subQueryPlan.data(), rstr.c_str(), rstr.length());
				send_(*subQueryPlan_push_socket[nbr_of_subQueryPlans++], &subQueryPlan);
			}	
			nbr_of_results += nbr_of_subQueryPlans;		
		}
	}
			
	/////////////////////////////
	// Step2: receive the results of the sent <subQuery plans> from their corresponding <queryPlan_tasks>.
	zmq::socket_t *subQueryPlan_result_pull_socket; 
	subQueryPlan_result_pull_socket = sPool->takeSocket(id + _workerThread_index_per_level_[threadPool_level]);
	
	for (int result_nbr = 0; result_nbr != nbr_of_results; result_nbr++) {
		
		/////////////////////////////
		zmq::message_t subQueryPlanResult;  
		receive_(*subQueryPlan_result_pull_socket, &subQueryPlanResult);
				 queryPlan_result sub_qpResult = unwrap_queryPlan_result(std::string((char *)subQueryPlanResult.data(), 
																		 subQueryPlanResult.size()));									

		/////////////////////////////
		if (whoCalledMe) {
			for (int i = 0; i < qp.nodeList.size(); i++) {
			
				if (sub_qpResult.pruned_step1List[i] == true) qp.qpr.pruned_step1List[i] = true;
				if (sub_qpResult.pruned_step2List[i] == true) qp.qpr.pruned_step2List[i] = true;
			
				if (sub_qpResult.result_map[i].size() > 0)
					qp.qpr.result_map[i].insert(qp.qpr.result_map[i].end(), 
												sub_qpResult.result_map[i].begin(),
												sub_qpResult.result_map[i].end());

				if (sub_qpResult.binding_map[i].size() > 0) 
					qp.qpr.binding_map[i].insert(qp.qpr.binding_map[i].end(), 
												 sub_qpResult.binding_map[i].begin(),
												 sub_qpResult.binding_map[i].end());	
			}		
		}	
	}	
													  
	/////////////////
	if (whoCalledMe) {
		for (int i = 0; i < joint_children_index.size(); i++)
			if (qp.qpr.result_map[joint_children_index[i]].size() == 0)
				for (int j = 0; j < qp.qpr.result_map.size(); j++) {
					 qp.qpr.result_map[j].clear();
					 qp.qpr.binding_map[j].clear();
				}
					
	
		/////////////////
		// Apply The second step of pruning on <sNode>
		qp.prune_step2(sNode, sNode_result_index, nodeName);
	
		/////////////////
		if (qp.qpr.result_map[sNode_result_index].size() == 0)
			for (int j = 0; j < qp.qpr.result_map.size(); j++) {
				qp.qpr.result_map[j].clear();
				qp.qpr.binding_map[j].clear();
			}	
	}
			
	//////////////////////////////
	// Step3: send the result of <qp> to the corresponding <query plan task> or <query task>. 
			 
	string endpoint = "tcp://" + qp.sender_ip + ":" + qp.sender_port;
  
	zmq::socket_t *queryPlan_result_push_socket; 
	queryPlan_result_push_socket = sPool->takeSocket(id + _workerThread_index_per_level_[threadPool_level],
													0, endpoint);
   
	/////////////////////////////
	if (!whoCalledMe) {
		qp.qpr.result_map.resize(0);
		qp.qpr.binding_map.resize(0);
		qp.qpr.pruned_step1List.resize(0);
		qp.qpr.pruned_step2List.resize(0);
		std::cout <<"!whocalledMe" <<  qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " << nodeName << ">"
			<< "from where=" << qp.sender_ip<< std::endl ;
	}
	
	string rstr = wrap_queryPlan_result(qp.qpr);
	zmq::message_t queryPlanResult(rstr.length());
	memcpy((void *) queryPlanResult.data(), rstr.c_str(), rstr.length());
	send_(*queryPlan_result_push_socket, &queryPlanResult);
			
	if (whoCalledMe) {
		///////////////// Information after prune-step2
		if (qp.qpr.pruned_step2List[sNode_result_index]) {
			cout << std::endl << BOLDGREEN << "After pruning-step2 of " << "<"
				<< qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << ">" << " at " << "<"
				<< nodeName << ">" << std::endl 
				<<"currentNode=" << currentNode << "1) The binding size: " << qp.qpr.binding_map[sNode_result_index].size() << std::endl 
				<< "2) The RDF result size: " << qp.qpr.result_map[sNode_result_index].size() 
				<< "from where=" << qp.sender_ip<< std::endl << RESET;
		}
		else 				
			cout << std::endl << BOLDWHITE << " *** No pruning-step2 of "
				 << "<" << qp.qpr.subListOrObjList_Set[sNode_result_index].get(0).getName() << "> at <" 
				 << nodeName << ">" << " ***"  << "currentNode=" << currentNode<< "from where=" << qp.sender_ip <<RESET << std::endl;
						
	}
}

//////////////////////////////////////////////////////////////////////////////////
// This server waits to receive <sub-query plans>.
void subQueryPlan_proxy(int currentNode, HeliosConfig* config, Memory* memory) { 	
   	printf("subQueryPlan_proxy thread lwpid = %u currentNode=%d\n", syscall(SYS_gettid),currentNode); 
	/////////////////////////
	// Third pool of worker threads, whereby the evaluation task of each <sub-query plan> is assigned to
	// an individual worker.
	// Third pool has a level equal to 2.
	ctpl::thread_pool Thread_Pool(config->_workerThread_nbr_per_level_[2], 2, currentNode, config, memory);	

	/////////////////////////
	// Create <subQueryPlan_pull_socket> in order to receive <sub-query plans>.
	zmq::context_t context(1);
	zmq::socket_t subQueryPlan_pull_socket (context, ZMQ_PULL);
	bind_(subQueryPlan_pull_socket, "tcp://*:" + subQueryPlan_proxy_port); 
	  
	/////////////////////////
	std::cout << std::endl;
	std::cout << "The subQueryPlan proxy started successfully." << std::endl;
	
	/////////////////////////
	zmq::message_t sub_qp_msg;
	
	/////////////////////////
	// each <sub-query plan> has the same structure as <query plan>.
	queryPlan qp; 
	 
  	/////////////////////////
	// Wait for recieving <sub-query plans> from a <(sub-)query plan> task, 
	// and assign the evaluation task of each <sub-query plan> to an individual worker.
	while (true) {  	
		/////////////////////////
		receive_(subQueryPlan_pull_socket, &sub_qp_msg);
		
		/////////////////////////
	    qp = unwrap_queryPlan(std::string((char *)sub_qp_msg.data(), sub_qp_msg.size()));
			
  	    /////////////////////////
		Thread_Pool.push(queryPlan_task_local, 2, qp, currentNode, config, memory, true);				
    }  
} 

//////////////////////////////////////////////////////////////////////////////////
// This server waits to receive <query plans>.
void queryPlan_proxy(int currentNode, HeliosConfig* config, Memory* memory) { 
	printf("queryPlan_proxy thread lwpid = %u currentNode=%d\n", syscall(SYS_gettid),currentNode);
	/////////////////////////
	// Second pool of worker threads, whereby each <query plan> task is assigned to a worker.
	// Second pool has a level equal to 1.
	ctpl::thread_pool Thread_Pool(config->_workerThread_nbr_per_level_[1], 1, currentNode, config, memory);	
	
	/////////////////////////
	// Create queryPlan_pull_socket in order to receive query plans
	zmq::context_t context(1);
	zmq::socket_t queryPlan_pull_socket (context, ZMQ_PULL);
	bind_(queryPlan_pull_socket, "tcp://*:" + queryPlan_proxy_port); 
	  
	/////////////////////////
	std::cout << std::endl;
	std::cout << "The queryPlan proxy started successfully." << std::endl;
	
	/////////////////////////
	zmq::message_t qp_msg;
	queryPlan qp;
	
  	/////////////////////////
	// Wait for recieving <query plans> from <query tasks>, 
	// and assign the evaluation task of each <query plan> to an individual worker.
	while (true) {  	
		//////////////////////////
		receive_(queryPlan_pull_socket, &qp_msg);
		
		/////////////////////////
	    qp = unwrap_queryPlan(std::string((char *)qp_msg.data(), qp_msg.size()));
			
  	    /////////////////////////
		Thread_Pool.push(queryPlan_task_local, 1, qp, currentNode, config, memory, false);		
    }  
} 

//////////////////////////////////////////////////////////////////////////////////
// Generate the <query plan(s)> of the assigned query <query>.
void query_task(int id, Query query, int currentNode, HeliosConfig* config) {
	/////////////////////////
	// Create the query plan graph of the <query>.
	std::string current_nodeName, target_nodeName;
	queryPlan_generator QPG(query, dbPool->get(id));
	QPG.output();
	 
	/////////////////////////
	vector<int> s_list = QPG.getServers();
	int nbr_of_queryPlans = s_list.size(); 
	if (nbr_of_queryPlans > 10) {
		cerr << "Invalid Clusetr size!" << std::endl;
		exit (1); }
	 
	/////////////////////////
	current_nodeName = config->host_names[currentNode];
  
	/////////////////////////////
	std::vector<zmq::socket_t*> queryPlan_push_socket(10);
 
	/////////////////////////////
	// Step1: send the <query plans> of the <query> to their corresponding <query plan proxies>.
	for (int i = 0; i < nbr_of_queryPlans; i++ ) {
		
		/////////////////////////////
		target_nodeName = config->host_names[s_list[i]];
		queryPlan_push_socket[i] = sPool->takeSocket(id, i, "tcp://" + target_nodeName + ':' + queryPlan_proxy_port);
	 
		/////////////////////////////
		queryPlan qp = QPG.get_queryPlan();
		qp.setPort(sPool->takePortNo(id));
	    qp.setIP(current_nodeName);
		
		/////////////////////////////
		string rstr = wrap_queryPlan(qp);
	    zmq::message_t queryPlan(rstr.length());
		memcpy((void *)queryPlan.data(), rstr.c_str(), rstr.length());
		send_(*queryPlan_push_socket[i], &queryPlan);
	}
 
	/////////////////////////////
	// Step2: receive the results of the <query plans> from their corresponding <query plan tasks>.
	zmq::socket_t *queryPlan_result_pull_socket; 
	queryPlan_result_pull_socket = sPool->takeSocket(id);
	
	//////////////////////////////
	// We just need to return the results and boundings for the required variables existing 
	// in the head of the query.
	query.binding_map.resize(query.head.size());
	query.result_map.resize(query.head.size());
	
	//////////////////////////////
	// maximum time elapsed for sending and calculating each query plan
	query.time_elapsed_for_queryPlans = 0;
	query.time_elapsed_for_evaluations = 0;
	
	for (int result_nbr = 0; result_nbr != nbr_of_queryPlans; result_nbr++) {
		
		//////////////////////////////
		zmq::message_t queryPlanResult;  
		receive_(*queryPlan_result_pull_socket, &queryPlanResult);
		queryPlan_result qpResult = unwrap_queryPlan_result(std::string((char *)queryPlanResult.data(), 
		                                                    queryPlanResult.size()));

        /////////////////////////////
	    // maximum response time elapsed for a query plan
		if ((get_usec() - qpResult.time_sent) > query.time_elapsed_for_queryPlans)
			query.time_elapsed_for_queryPlans = (get_usec() - qpResult.time_sent);
				
		/////////////////////////////
	    // maximum processing time for the evaluation of a query plan 
		if (qpResult.eval_time > query.time_elapsed_for_evaluation)
			query.time_elapsed_for_evaluation = qpResult.eval_time;
		
		/////////////////////////////
		// Fill <query.binding_map> and <query.result_map> with to the results in <qpResult> 
		// for (int i = 0; i < query.head.size(); i++)
			// for (int j = 0; j < qpResult.subListOrObjList_Set.size(); j++)
				// if (!qpResult.subListOrObjList_Set[j].isBound())
					// if (qpResult.subListOrObjList_Set[j].get(0).getName() == query.head[i].getName()) {
						// // query.result_map[i] += qpResul.result_map[i]
						// // query.binding_map[i] += qpResult.binding_map[i]
						// query.binding_map[i].insert(query.binding_map[i].end(), 
													// qpResult.binding_map[j].begin(), 
													// qpResult.binding_map[j].end());
						// query.result_map[i].insert(query.result_map[i].end(), 
												   // qpResult.result_map[j].begin(), 
												   // qpResult.result_map[j].end());	
						// /////////////////
						// break;
					// }
	}	
		
	//////////////////////////////
	// It is used for deduplicating the content of results in query.result_map
	// for (int i = 0; i < query.result_map.size(); i++) {
		// std::sort(query.result_map[i].begin(), query.result_map[i].end());
		// dedup_triples(query.result_map[i]);
	// }
	
	//////////////////////////////
	// It is used for deduplicating the content of results in query.binding_map
	// std::set <sid_t> res1;
	// for (int i = 0; i < query.binding_map.size(); i++) {
	
		// //////////////////////////////
		// // deduplication of <query.binding_map>
		// res1.clear();
		// for (int k = 0; k < query.binding_map[i].size(); k++)
			// res1.insert(query.binding_map[i][k]);
		// query.binding_map[i].assign(res1.begin(), res1.end());
	// }
	std::cout << "Step3: send the result of the <query> to the corresponding <client task>." << "currentNode=" << currentNode << std::endl;										   
	////////////////////////////
	// Step3: send the result of the <query> to the corresponding <client task>. 
	string client_endpoint = "tcp://" + query.getIP() + ":" + query.getPort();
  
	zmq::socket_t *query_result_push_socket; 
	query_result_push_socket = sPool->takeSocket(id, 0, client_endpoint);
   
	////////////////////////////
	// For now, we decrease the time of data transmission, by making empty the query sent to the client
	query.binding_map.resize(query.head.size());
	query.result_map.resize(query.head.size());
	
	string rstr = wrap_query(query);
	zmq::message_t queryResult(rstr.length());
	memcpy((void *) queryResult.data(), rstr.c_str(), rstr.length());
	send_(*query_result_push_socket, &queryResult);
	
	// Prints the result of the query.
	// std::cout << std::endl << BOLDBLUE << "A query task hosted at " << "<" << current_nodeName << ">"
		      // << " assembled the result of query#" << query.queryNo << ", as the following:"
			  // << RESET << std::endl;	
				  
		// /////////////////////////////
		// for (int i = 0; i < query.head.size(); i++) {
			// cout << std::endl << BOLDBLUE << "The size of RDF instances whose source is " << "<"
			     // << query.head[i].getName() << "> : " << query.result_map[i].size() << RESET;
			// cout << std::endl << BOLDBLUE << "The binding size of " << query.head[i].getName() << ": "
				 // << query.binding_map[i].size() << RESET;
		// }
} 

//////////////////////////////////////////////////////////////////////////////////
// This server waits to receive <queries>.
void query_proxy(int currentNode, HeliosConfig* config, Memory* memory) { 
        printf("query_proxy thread lwpid = %u currentNode=%d\n", syscall(SYS_gettid),currentNode);	
	/////////////////////////////
	Database *db = new Database(currentNode, config, memory);
	//nodeName = config->host_names[currentNode];
	/////////////////////////
	// WATDiv_queryBank QB;
	// LUBM_queryBank QB;
	// QB.output(db);
	
	/////////////////////////
	// First pool of worker threads, whereby each <query> task is assigned to a worker.
	// First pool has a level equal to 0.
	ctpl::thread_pool Thread_Pool(config->_workerThread_nbr_per_level_[0], 0, currentNode, config, memory);

	/////////////////////////
	// Create query_pull_socket in order to receive queries
	zmq::context_t context(1);
	zmq::socket_t query_pull_socket (context, ZMQ_PULL); 
	bind_(query_pull_socket, "tcp://*:" + query_proxy_port); 

	/////////////////////////
	std::cout << std::endl;
	std::cout << "The query proxy started successfully." << std::endl;

    /////////////////////////
	zmq::message_t query;
	Query q;
	
	/////////////////////////
	// Wait for recieving queries from clients, and assign each one (for the evaluation) to a query task.
	while (true) {  
		/////////////////////////
		receive_(query_pull_socket, &query);

		/////////////////////////
		q = unwrap_query(std::string((char *)query.data(), query.size()));
		// cout << "Decompressed: " << std::endl;
		// cout << q.output(db) << std::endl;	
	
		/////////////////////////
		q.compress (db);
		// cout << "Compressed: " << std::endl;
		cout << endl << "------------------------";
		cout << endl << "Query:";
		cout << endl << "------------------------" << endl;
		cout << endl << "pattern_value=" << q.query_pattern << std::endl;
		cout << q.output(db) << std::endl;	
		
		/////////////////////////
		Thread_Pool.push(query_task, q, currentNode, config); 
	}  
}

