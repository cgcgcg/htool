#ifndef HTOOL_CLUSTERING_CLUSTER_HPP
#define HTOOL_CLUSTERING_CLUSTER_HPP

#include "../types/matrix.hpp"
#include "../types/point.hpp"
#include "../misc/parametres.hpp"


namespace htool {


template<typename Derived>
class Cluster: public Parametres{
protected:
    // Data member
	std::vector<Derived*> sons;    // Sons

    int rank;    // Rank for dofs of the current cluster
	int depth;   // depth of the current cluster
	int counter; // numbering of the nodes level-wise

    double rad;
    R3     ctr;

	int max_depth;
	int min_depth;
	int offset;
	int size;

	std::shared_ptr<std::vector<int>> permutation;

	Derived* local_cluster;
	Derived* root;
	std::vector<std::pair<int,int>> MasterOffset;

	// Root constructor
	Cluster():depth(0),counter(0),max_depth(0),min_depth(-1),offset(0),permutation(std::make_shared<std::vector<int>>()),root(static_cast<Derived*>(this)),local_cluster(nullptr){}

	// Node constructor
	Cluster(Derived* root0, int counter0, const int& dep,std::shared_ptr<std::vector<int>> permutation0):ctr(), rad(0.),max_depth(-1),min_depth(-1), offset(0), root(root0),counter(counter0),permutation(permutation0) {
		for (auto & son : sons){
			son=0;
		}
		depth = dep;
	}

	// Destructor
    ~Cluster(){
        for (int p=0;p<sons.size();p++){
            if (sons[p]!=nullptr){ delete sons[p];sons[p]=nullptr;}
        }
    };

public:

    // default build cluster tree
	void build(const std::vector<R3>& x0, const std::vector<double>& r0,const std::vector<int>& tab0, const std::vector<double>& g0, int nb_sons = -1, MPI_Comm comm=MPI_COMM_WORLD){
		static_cast<Derived*>(this)->build( x0, r0,tab0, g0, nb_sons, comm);
	}

   
	//// Getters for local data
	const double&   get_rad() const {return rad;}
	const R3&       get_ctr() const {return ctr;}
	const Derived&  get_son(const int& j) const {return *(sons[j]);}
	Derived&        get_son(const int& j){return *(sons[j]);}
	int get_depth() const {return depth;}
	int get_rank()const {return rank;}
	int get_offset() const {return offset;}
	int get_size() const {return size;}
	int get_nb_sons() const {return sons.size();}
	int get_counter() const {return counter;}
	const Derived& get_local_cluster( MPI_Comm comm=MPI_COMM_WORLD) const {
		int rankWorld, sizeWorld;
		MPI_Comm_size(comm, &sizeWorld);
		MPI_Comm_rank(comm, &rankWorld);

		return *(root->local_cluster);
	}

	std::shared_ptr<Derived> get_local_cluster_tree(MPI_Comm comm=MPI_COMM_WORLD){
		int rankWorld;
		MPI_Comm_rank(comm, &rankWorld);

		std::shared_ptr<Derived> copy_local_cluster=std::make_shared<Derived>();

		copy_local_cluster->MasterOffset.push_back(std::make_pair(this->MasterOffset[rankWorld].first,this->MasterOffset[rankWorld].second));

		copy_local_cluster->local_cluster=copy_local_cluster->root;

		copy_local_cluster->permutation=this->permutation;



		// Recursion
		std::stack<Derived*> cluster_input;
		cluster_input.push(local_cluster);
		std::stack<Derived*> cluster_output;
		cluster_output.push(copy_local_cluster->root);
		int count = 0;
		while(!cluster_input.empty()){
			Derived* curr_input  = cluster_input.top();
			Derived* curr_output = cluster_output.top();
			
			cluster_input.pop();
			cluster_output.pop();

			curr_output->rank    = curr_input->rank;
			curr_output->ctr     = curr_input->ctr;
			curr_output->rad     = curr_input->rad;
			curr_output->offset  = curr_input->offset;
			curr_output->size    = curr_input->size;

			int nb_sons = curr_input->sons.size();
			curr_output->sons.resize(nb_sons);
			for (int p=0;p<nb_sons;p++){
				curr_output->sons[p] = new Derived(copy_local_cluster.get(),(curr_output->counter)*nb_sons+p,curr_output->depth+1,this->permutation);

				cluster_input.push(curr_input->sons[p]);
				cluster_output.push(curr_output->sons[p]);
			}
		}

		
		return copy_local_cluster;
	}

	//// Getters for global data
	int	get_max_depth() const {return root->max_depth;}
	int	get_min_depth() const {return root->min_depth;}
	const std::vector<int>& get_perm() const{return *permutation;};
	int get_perm(int i) const{return (*permutation)[i];};
	std::vector<int>::const_iterator get_perm_start() const {return permutation->begin();}
	const Derived& get_root() const {
		return *(root);
	}

	//// Getter for MasterOffsets
	int get_local_offset() const {return root->local_cluster->get_offset();}
    int get_local_size() const {return root->local_cluster->get_size();}
    std::pair<int,int> get_masteroffset(int i)const {return root->MasterOffset[i];}

    // Permutations
	template<typename T>
	void cluster_to_global(const T* const in, T* const out){
		for (int i = 0; i<permutation->size();i++){
			out[(*permutation)[i]]=in[i];
		}
	}
	
	template<typename T>
	void global_to_cluster(const T* const in, T* const out){
		for (int i = 0; i<permutation->size();i++){
			out[i]=in[(*permutation)[i]];
		}
	}

    // void get_offset(std::vector<int> & J, int i) const;
	// void get_size(std::vector<int> & J, int i) const;
	// void get_ctr(std::vector<R3> & ctrs, int i) const;

	//// Setters
	void set_rank  (int rank0)   {rank = rank0;}
	void set_offset(int offset0) {offset=offset0;}
	void set_size(int size0) {size=size0;}


    bool IsLeaf() const { if(sons.size()==0){return true;} return false; }

	// Output
	void print(MPI_Comm comm=MPI_COMM_WORLD) const{
		int rankWorld;
		MPI_Comm_rank(MPI_COMM_WORLD, &rankWorld);
		if (rankWorld==0){
			if ( !permutation->empty() ) {
				std::cout << '[';
				for (std::vector<int>::const_iterator i = permutation->begin()+offset; i != permutation->begin()+offset+size; ++i)
				std::cout << *i << ',';
				std::cout << "\b]"<<std::endl;;
			}
			// std::cout << offset << " "<<size << std::endl;
			for (auto & son : this->sons){
				if (son!=NULL) (*son).print();
			}
		}
	}

	void save(const std::vector<R3>& x0, std::string filename, const std::vector<int>& depths,MPI_Comm comm=MPI_COMM_WORLD) const{
		int rankWorld;
		MPI_Comm_rank(comm, &rankWorld);
		if (rankWorld==0){

			std::stack< Cluster<Derived> const *> s;
			s.push(this);
			std::ofstream output(filename);

			std::vector<std::vector<int>> outputs(depths.size());
			std::vector<int> counters(depths.size(),0);
			for (int p=0;p<outputs.size();p++){
				outputs[p].resize(permutation->size());
			}

			// Permuted geometric points
			for (int d=0;d<3;d++){
				output << "x_"<<d<<",";
				for(int i = 0; i<permutation->size(); ++i) {
					output << x0[(*permutation)[i]][d];
					if (i!=permutation->size()-1){
						output << ",";
					}
				}
				output << "\n";
			}

			while(!s.empty()){
				Cluster<Derived> const * curr = s.top();
				s.pop();
				std::vector<int>::const_iterator it =  std::find(depths.begin(), depths.end(), curr->depth);

				if (it != depths.end()){
					int index = std::distance(depths.begin(), it);
					std::fill_n(outputs[index].begin()+curr->offset,curr->size,counters[index]);
					counters[index]+=1;
				}

				// Recursion
				if (!curr->IsLeaf()){
					
					for (int p=0;p<curr->get_nb_sons();p++){
						s.push((curr->sons[p]));
					}
				}

			}

			for (int p=0;p<depths.size();p++){
				output<<depths[p]<<",";

				for(int i = 0; i < outputs[p].size(); ++i) {
					output << outputs[p][i] ;
					if (i!=outputs[p].size()-1){
						output << ',';
					}
				}
				output << "\n";
			}
		}
	}
};

}
#endif