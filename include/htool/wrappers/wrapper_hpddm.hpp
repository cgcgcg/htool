#ifndef HTOOL_WRAPPER_HPDDM_HPP
#define HTOOL_WRAPPER_HPDDM_HPP

#define HPDDM_NUMBERING 'F'
#define HPDDM_DENSE 1
#define HPDDM_FETI 0
#define HPDDM_BDD 0
#define LAPACKSUB
#define DLAPACK
#define EIGENSOLVER 1
#include <HPDDM.hpp>
#include "../types/hmatrix.hpp"
#include "../types/matrix.hpp"
#include "../solvers/proto_ddm.hpp"

namespace htool{

template<template<typename> class LowRankMatrix, typename T>
class DDM;

template<template<typename> class LowRankMatrix, typename T>
class Proto_DDM;

template< template<typename> class LowRankMatrix, typename T>
class HPDDMDense : public HpDense<T, 'G'> {
private:
    const HMatrix<LowRankMatrix,T>& HA;
    std::vector<T>* in_global,*buffer;


public:
    typedef  HpDense<T, 'G'> super;

    HPDDMDense(const HMatrix<LowRankMatrix,T>& A):HA(A){
        in_global = new std::vector<T> ;
        buffer = new std::vector<T>;
    }
    ~HPDDMDense(){delete in_global;in_global=nullptr;delete buffer;buffer=nullptr;}


    virtual void GMV(const T* const in, T* const out, const int& mu = 1) const override {
        int local_size = HA.get_local_size();

        // Tranpose without overlap
        if (mu!=1){
            for (int i=0;i<mu;i++){
                for (int j=0;j<local_size;j++){
                    (*buffer)[i+j*mu]=in[i*this->getDof()+j];
                }
            }
        }

        // All gather
        if (mu==1){// C'est moche
            HA.mvprod_local(in,out,in_global->data(),mu);
        }
        else{
            HA.mvprod_local(buffer->data(),buffer->data()+local_size*mu,in_global->data(),mu);
        }



        // Tranpose
        if (mu!=1){
            for (int i=0;i<mu;i++){
                for (int j=0;j<local_size;j++){
                    out[i*this->getDof()+j]=(*buffer)[i+j*mu+local_size*mu];
                }
            }
        }
        bool allocate = this->getMap().size() > 0 && this->getBuffer()[0] == nullptr ? this->setBuffer() : false;
        this->exchange(out, mu);
        if(allocate)
            this->clearBuffer(allocate);
    }

    void scaledexchange(T* const out, const int& mu = 1) const{
    this->template exchange<true>(out, mu);
    }

    friend class DDM<LowRankMatrix,T>;

};

template< template<typename> class LowRankMatrix, typename T>
class Proto_HPDDM : public HpDense<T, 'G'> {
private:
    const HMatrix<LowRankMatrix,T>& HA;
    std::vector<T>* in_global;
    Proto_DDM<LowRankMatrix,T>& P;

public:
    typedef  HpDense<T, 'G'> super;

    Proto_HPDDM(const HMatrix<LowRankMatrix,T>& A,  Proto_DDM<LowRankMatrix,T>& P0):HA(A),P(P0){
        in_global = new std::vector<T> (A.nb_cols());
        P.init_hpddm(*this);
    }
    ~Proto_HPDDM(){delete in_global;}

    void GMV(const T* const in, T* const out, const int& mu = 1) const {
        HA.mvprod_local(in,out,in_global->data(),1);
    }

    template<bool = true>
    void apply(const T* const in, T* const out, const unsigned short& mu = 1, T* = nullptr, const unsigned short& = 0) const {
        P.apply(in,out);
        // std::copy_n(in, this->_n, out);
    }

    void build_coarse_space(Matrix<T>& Mi, IMatrix<T>& generator_Bi, const std::vector<R3>& x){
        // Coarse space
        P.build_coarse_space(Mi,generator_Bi,x);
    }

    void build_coarse_space(Matrix<T>& Mi, const std::vector<R3>& x){
        // Coarse space
        P.build_coarse_space(Mi,x);
    }

    void facto_one_level(){
        P.facto_one_level();
    }
    void solve(const T* const rhs, T* const x, const int& mu=1 ){
        //
        int rankWorld = HA.get_rankworld();
        int sizeWorld = HA.get_sizeworld();
        int offset  = HA.get_local_offset();
        int nb_cols = HA.nb_cols();
        int nb_rows = HA.nb_rows();
        double time = MPI_Wtime();
        int n = P.get_n();
        int n_inside = P.get_n_inside();
        double time_vec_prod = StrToNbr<double>(HA.get_infos("total_time_mat_vec_prod"));
        int nb_vec_prod =  StrToNbr<int>(HA.get_infos("nb_mat_vec_prod"));
        P.timing_Q=0;
        P.timing_one_level=0;

        //
        std::vector<T> rhs_perm(nb_cols);
        std::vector<T> x_local(n,0);
        std::vector<T> local_rhs(n,0);

        // Permutation
        HA.source_to_cluster_permutation(rhs,rhs_perm.data());
        std::copy_n(rhs_perm.begin()+offset,n_inside,local_rhs.begin());


        // Solve
        int nb_it = HPDDM::IterativeMethod::solve(*this, local_rhs.data(), x_local.data(), mu,HA.get_comm());

        // // Delete the overlap (useful only when mu>1 and n!=n_inside)
        // for (int i=0;i<mu;i++){
        //     std::copy_n(x_local.data()+i*n,n_inside,local_rhs.data()+i*n_inside);
        // }

        // Local to global
        // hpddm_op.HA.local_to_global(x_local.data(),hpddm_op.in_global->data(),mu);
        std::vector<int> recvcounts(sizeWorld);
        std::vector<int>  displs(sizeWorld);

        displs[0] = 0;

        for (int i=0; i<sizeWorld; i++) {
        recvcounts[i] = (HA.get_MasterOffset_t(i).second);
        if (i > 0)
            displs[i] = displs[i-1] + recvcounts[i-1];
        }

        MPI_Allgatherv(x_local.data(), recvcounts[rankWorld], wrapper_mpi<T>::mpi_type(), in_global->data() , &(recvcounts[0]), &(displs[0]), wrapper_mpi<T>::mpi_type(), HA.get_comm());

        // Permutation
        HA.cluster_to_target_permutation(in_global->data(),x);

        // Infos
        HPDDM::Option& opt = *HPDDM::Option::get();
        time = MPI_Wtime()-time;
        P.set_infos("Solve",NbrToStr(time));
        P.set_infos("Nb_it",NbrToStr(nb_it));
        P.set_infos("Nb_subdomains",NbrToStr(sizeWorld));
        P.set_infos("nb_mat_vec_prod",NbrToStr(StrToNbr<int>(HA.get_infos("nb_mat_vec_prod"))-nb_vec_prod));
        P.set_infos("mean_time_mat_vec_prod",NbrToStr((StrToNbr<double>(HA.get_infos("total_time_mat_vec_prod"))-time_vec_prod)/(StrToNbr<double>(HA.get_infos("nb_mat_vec_prod"))-nb_vec_prod)));
        switch (opt.val("schwarz_method",0)) {
            case HPDDM_SCHWARZ_METHOD_NONE:
            P.set_infos("Precond","None");
            break;
            case HPDDM_SCHWARZ_METHOD_RAS:
            P.set_infos("Precond","RAS");
            break;
            case HPDDM_SCHWARZ_METHOD_ASM:
            P.set_infos("Precond","ASM");
            break;
            case HPDDM_SCHWARZ_METHOD_OSM:
            P.set_infos("Precond","OSM");
            break;
            case HPDDM_SCHWARZ_METHOD_ORAS:
            P.set_infos("Precond","ORAS");
            break;
            case HPDDM_SCHWARZ_METHOD_SORAS:
            P.set_infos("Precond","SORAS");
            break;
        }

        switch (opt.val("krylov_method",8)) {
            case HPDDM_KRYLOV_METHOD_GMRES:
            P.set_infos("krylov_method","gmres");
            break;
            case HPDDM_KRYLOV_METHOD_BGMRES:
            P.set_infos("krylov_method","bgmres");
            break;
            case HPDDM_KRYLOV_METHOD_CG:
            P.set_infos("krylov_method","cg");
            break;
            case HPDDM_KRYLOV_METHOD_BCG:
            P.set_infos("krylov_method","bcg");
            break;
            case HPDDM_KRYLOV_METHOD_GCRODR:
            P.set_infos("krylov_method","gcrodr");
            break;
            case HPDDM_KRYLOV_METHOD_BGCRODR:
            P.set_infos("krylov_method","bgcrodr");
            break;
            case HPDDM_KRYLOV_METHOD_BFBCG:
            P.set_infos("krylov_method","bfbcg");
            break;
            case HPDDM_KRYLOV_METHOD_RICHARDSON:
            P.set_infos("krylov_method","richardson");
            break;
            case HPDDM_KRYLOV_METHOD_NONE:
            P.set_infos("krylov_method","none");
            break;
        }

        //
        if (P.get_infos("Precond")=="None"){
            P.set_infos("GenEO_coarse_size","0");
            P.set_infos("Coarse_correction","None");
            P.set_infos("DDM_local_coarse_size","0");
        }
        else{
            P.set_infos("GenEO_coarse_size",NbrToStr(P.get_size_E()));
            int nevi = P.get_nevi();
            P.set_infos("DDM_local_coarse_size",NbrToStr(nevi));
            if (rankWorld==0){
                MPI_Reduce(MPI_IN_PLACE, &(nevi),1, MPI_INT, MPI_SUM, 0,HA.get_comm());
            }
            else{
                MPI_Reduce(&(nevi), &(nevi),1, MPI_INT, MPI_SUM, 0,HA.get_comm());
            }
            P.set_infos("DDM_local_coarse_size_mean",NbrToStr((double)nevi/(double)sizeWorld));
            switch (opt.val("schwarz_coarse_correction",42)) {
                case HPDDM_SCHWARZ_COARSE_CORRECTION_BALANCED:
                P.set_infos("Coarse_correction","Balanced");
                break;
                case HPDDM_SCHWARZ_COARSE_CORRECTION_ADDITIVE:
                P.set_infos("Coarse_correction","Additive");
                break;
                case HPDDM_SCHWARZ_COARSE_CORRECTION_DEFLATED:
                P.set_infos("Coarse_correction","Deflated");
                break;
                default:
                P.set_infos("Coarse_correction","None");
                P.set_infos("GenEO_coarse_size","0");
                P.set_infos("DDM_local_coarse_size","0");
                break;
            }



        }
        P.set_infos("htool_solver","protoddm");
        
        double timing_one_level=P.get_timing_one_level();
        double timing_Q=P.get_timing_Q();
        double maxtiming_one_level,maxtiming_Q;

        // Timing
        MPI_Reduce(&(timing_one_level), &(maxtiming_one_level), 1, MPI_DOUBLE, MPI_MAX, 0,HA.get_comm());
        MPI_Reduce(&(timing_Q), &(maxtiming_Q), 1, MPI_DOUBLE, MPI_MAX, 0,HA.get_comm());

        P.set_infos("DDM_apply_one_level_max", NbrToStr(maxtiming_one_level));
        P.set_infos("DDM_apply_Q_max", NbrToStr(maxtiming_Q));

    }

    void print_infos() const{
        if (HA.get_rankworld()==0){
            for (std::map<std::string,std::string>::const_iterator it = P.get_infos().begin() ; it != P.get_infos().end() ; ++it){
                std::cout<<it->first<<"\t"<<it->second<<std::endl;
            }
        std::cout << std::endl;
        }
    }
    void save_infos(const std::string& outputname, std::ios_base::openmode mode = std::ios_base::app, const std::string& sep= " = ") const{
    	if (HA.get_rankworld()==0){
    		std::ofstream outputfile(outputname, mode);
    		if (outputfile){
                for (std::map<std::string,std::string>::const_iterator it = P.get_infos().begin() ; it != P.get_infos().end() ; ++it){
                    outputfile<<it->first<<sep<<it->second<<std::endl;
                }
    			outputfile.close();
    		}
    		else{
    			std::cout << "Unable to create "<<outputname<<std::endl;
    		}
    	}
    }


    void add_infos(std::string key, std::string value) const{
        if (HA.get_rankworld()==0){
            if (P.get_infos().find(key)==P.get_infos().end()){
                P.set_infos(key,value);
            }
            else{
                P.set_infos(key,value);
            }
        }
    }

    std::string get_infos (const std::string& key) const { return P.get_infos(key);}
};

template< template<typename> class LowRankMatrix, typename T>
class Calderon : public HPDDM::EmptyOperator<T> {
private:
    const HMatrix<LowRankMatrix,T>& HA;
    const HMatrix<LowRankMatrix,T>& HB;
    Matrix<T>& M;
    std::vector<int> _ipiv;
    std::vector<T>* in_global,*buffer;
    mutable std::map<std::string, std::string> infos;

public:

    Calderon(const HMatrix<LowRankMatrix,T>& A,  const HMatrix<LowRankMatrix,T>& B,  Matrix<T>& M0):HPDDM::EmptyOperator<T>(A.get_local_size()),HA(A),HB(B),M(M0),_ipiv(M.nb_rows()){
        in_global = new std::vector<T> ;
        buffer = new std::vector<T>;

        // LU facto
        int size = M.nb_rows();
        int lda=M.nb_rows();
        int info;
        HPDDM::Lapack<Cplx>::getrf(&size,&size,M.data(),&lda,_ipiv.data(),&info);


    }

    ~Calderon(){delete in_global;delete buffer;}


    void GMV(const T* const in, T* const out, const int& mu = 1) const {
        int local_size = HA.get_local_size();

        // Tranpose without overlap
        if (mu!=1){
            for (int i=0;i<mu;i++){
                for (int j=0;j<local_size;j++){
                    (*buffer)[i+j*mu]=in[i*this->getDof()+j];
                }
            }
        }

        // All gather
        if (mu==1){// C'est moche
            HA.mvprod_local(in,out,in_global->data(),mu);
        }
        else{
            HA.mvprod_local(buffer->data(),buffer->data()+local_size*mu,in_global->data(),mu);
        }



        // Tranpose
        if (mu!=1){
            for (int i=0;i<mu;i++){
                for (int j=0;j<local_size;j++){
                    out[i*this->getDof()+j]=(*buffer)[i+j*mu+local_size*mu];
                }
            }
        }

    }

    template<bool = true>
    void apply(const T* const in, T* const out, const unsigned short& mu = 1, T* = nullptr, const unsigned short& = 0) const {
        int local_size = HB.get_local_size();
        int offset = HB.get_local_offset();
        // Tranpose
        if (mu!=1){
            for (int i=0;i<mu;i++){
                for (int j=0;j<local_size;j++){
                    (*buffer)[i+j*mu]=in[i*this->getDof()+j];
                }
            }
        }

        // M^-1
        HA.local_to_global(in, in_global->data(),mu);
        const char l='N';
        int n= M.nb_rows();
        int lda=M.nb_rows();
        int ldb=M.nb_rows();
        int nrhs =mu ;
        int info;
        HPDDM::Lapack<T>::getrs(&l,&n,&nrhs,M.data(),&lda,_ipiv.data(),in_global->data(),&ldb,&info);

        // All gather
        if (mu==1){// C'est moche
            HB.mvprod_local(in_global->data()+offset,out,in_global->data()+M.nb_rows(),mu);
        }
        else{
            HB.mvprod_local(buffer->data(),buffer->data()+local_size*mu,in_global->data(),mu);
        }

        // M^-1
        HA.local_to_global(out, in_global->data(),mu);
        HPDDM::Lapack<T>::getrs(&l,&n,&nrhs,M.data(),&lda,_ipiv.data(),in_global->data()+M.nb_rows(),&ldb,&info);

        // Tranpose
        if (mu!=1){
            for (int i=0;i<mu;i++){
                for (int j=0;j<local_size;j++){
                    out[i*this->getDof()+j]=(*buffer)[i+j*mu+local_size*mu];
                }
            }
        }

    }


    void solve(const T* const rhs, T* const x, const int& mu=1 ){
        //
        int rankWorld = HA.get_rankworld();
        int sizeWorld = HA.get_sizeworld();
        int offset  = HA.get_local_offset();
        int nb_cols = HA.nb_cols();
        int nb_rows = HA.nb_rows();
        int n_local = this->_n;
        double time = MPI_Wtime();
        double time_vec_prod = StrToNbr<double>(HA.get_infos("total_time_mat_vec_prod"));
        int nb_vec_prod =  StrToNbr<int>(HA.get_infos("nb_mat_vec_prod"));
        in_global->resize(nb_cols*2*mu);
        buffer->resize(n_local*(mu==1 ? 1 : 2*mu));

        //
        std::vector<T> rhs_perm(nb_cols);
        std::vector<T> x_local(n_local,0);
        std::vector<T> local_rhs(n_local,0);

        // Permutation
        HA.source_to_cluster_permutation(rhs,rhs_perm.data());
        std::copy_n(rhs_perm.begin()+offset,n_local,local_rhs.begin());

        // Solve
        int nb_it = HPDDM::IterativeMethod::solve(*this, local_rhs.data(), x_local.data(), mu,HA.get_comm());

        // // Delete the overlap (useful only when mu>1 and n!=n_inside)
        // for (int i=0;i<mu;i++){
        //     std::copy_n(x_local.data()+i*n,n_inside,local_rhs.data()+i*n_inside);
        // }

        // Local to global
        // hpddm_op.HA.local_to_global(x_local.data(),hpddm_op.in_global->data(),mu);
        std::vector<int> recvcounts(sizeWorld);
        std::vector<int>  displs(sizeWorld);

        displs[0] = 0;

        for (int i=0; i<sizeWorld; i++) {
        recvcounts[i] = (HA.get_MasterOffset_t(i).second);
        if (i > 0)
            displs[i] = displs[i-1] + recvcounts[i-1];
        }

        MPI_Allgatherv(x_local.data(), recvcounts[rankWorld], wrapper_mpi<T>::mpi_type(), in_global->data() , &(recvcounts[0]), &(displs[0]), wrapper_mpi<T>::mpi_type(), HA.get_comm());

        // Permutation
        HA.cluster_to_target_permutation(in_global->data(),x);

        // Timing
        HPDDM::Option& opt = *HPDDM::Option::get();
        time = MPI_Wtime()-time;
        infos["Solve"] = NbrToStr(time);
        infos["Nb_it"] = NbrToStr(nb_it);
        infos["nb_mat_vec_prod"] = NbrToStr(StrToNbr<int>(HA.get_infos("nb_mat_vec_prod"))-nb_vec_prod);
        infos["mean_time_mat_vec_prod"] = NbrToStr((StrToNbr<double>(HA.get_infos("total_time_mat_vec_prod"))-time_vec_prod)/(StrToNbr<double>(HA.get_infos("nb_mat_vec_prod"))-nb_vec_prod));


    }

    void print_infos() const{
        if (HA.get_rankworld()==0){
            for (std::map<std::string,std::string>::const_iterator it = infos.begin() ; it != infos.end() ; ++it){
                std::cout<<it->first<<"\t"<<it->second<<std::endl;
            }
        std::cout << std::endl;
        }
    }
    void save_infos(const std::string& outputname, std::ios_base::openmode mode = std::ios_base::app, const std::string& sep= " = ") const{
    	if (HA.get_rankworld()==0){
    		std::ofstream outputfile(outputname, mode);
    		if (outputfile){
                for (std::map<std::string,std::string>::const_iterator it = infos.begin() ; it != infos.end() ; ++it){
                    outputfile<<it->first<<sep<<it->second<<std::endl;
                }
    			outputfile.close();
    		}
    		else{
    			std::cout << "Unable to create "<<outputname<<std::endl;
    		}
    	}
    }

    void add_infos(std::string key, std::string value) const{
        if (HA.get_rankworld()==0){
            if (infos.find(key)==infos.end()){
                infos[key]=infos[key]+value;
            }
            else{
                infos[key]=infos[key]+value;
            }
        }
    }

    std::string get_infos(const std::string& key) const { return infos[key];}
};

}
#endif
