#include <htool/htool.hpp>

using namespace std;
using namespace htool;

class MyMatrix: public IMatrix<double>{
    const vector<R3>& p1;
    const vector<R3>& p2;

public:
    // Constructor
    MyMatrix(const vector<R3>& p10,const vector<R3>& p20 ):
        IMatrix(p10.size(),p20.size()),p1(p10),p2(p20) {}

    // Virtual function to overload
    double get_coef(const int& k, const int& j)const {
        return 1./(1e-5+norm2(p1[j]-p2[k]));
    }

    // Matrix vector product
	std::vector<double> operator*(std::vector<double> a){
		std::vector<double> result(p1.size(),0);
		for (int j=0;j<p1.size();j++){
			for (int k=0;k<p2.size();k++){
				result[j]+=this->get_coef(j,k)*a[k];
			}
		}
        return result;
    }

    // Frobenius norm
    double norm(){
        double norm = 0;
        for (int j=0;j<p1.size();j++){
            for (int k=0;k<p2.size();k++){
                norm+=this->get_coef(j,k);
            }
        }
        return norm;
    }
};


int main(int argc, char *argv[]) {

    // Initialize the MPI environment
    MPI_Init(&argc,&argv);

    // Htool parameters
	SetEpsilon(0.001);
	SetEta(100);
    SetMinClusterSize(10);

    // Data
    int n = 1000;
    vector<int> I(n); // indices for the hmatrix

    // p1: points in a square in the plane z=z1
    double z = 1;
    vector<R3> p(n);
    int size = sqrt(n); // sqrt(n) must be an integer !!!
    for(int j=0; j<size; j++){
        for(int k=0; k<size; k++){
            I[j+k*size] = j+k*size;
            p[j+k*size][0] = j;
            p[j+k*size][1] = k;
            p[j][2] = z;
        }
    }

    // Hmatrix
    MyMatrix A(p,p);
    std::vector<double> x(n,1),result(n,0);
    HMatrix<partialACA,double> HA(A,p,p);
    result = HA*x;

    // Output
    HA.print_infos();
    HA.save_plot("smallest_example_plot");
    std::cout<< Frobenius_absolute_error(HA,A)/A.norm()<<std::endl;
    std::cout<< norm2(A*x-result)/norm2(A*x)<<std::endl;

    // Finalize the MPI environment.
    MPI_Finalize();
}