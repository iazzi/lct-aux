#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <mutex>
#include <functional>

#include "helpers.hpp"
#include "measurements.hpp"
#include "weighted_measurements.hpp"


extern "C" {
#include <fftw3.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <Eigen/Dense>
#include <Eigen/QR>

static const double pi = 3.141592653589793238462643383279502884197;

class Configuration {
	private:
	int Lx, Ly, Lz; // size of the system
	int V; // volume of the system
	int N; // number of time-steps
	double beta; // inverse temperature
	double dt; // time step 
	double g; // interaction strength
	double mu; // chemical potential
	double A; // sqrt(exp(g*dt)-1)
	double B; // magnetic field
	double tx, ty, tz; // nearest neighbour hopping

	std::vector<Eigen::VectorXd> diagonals;

	std::mt19937_64 generator;
	std::bernoulli_distribution distribution;
	std::uniform_int_distribution<int> randomPosition;
	std::uniform_int_distribution<int> randomTime;
	std::exponential_distribution<double> trialDistribution;

	Eigen::VectorXd energies;
	Eigen::VectorXd freePropagator;
	Eigen::VectorXd freePropagator_b;

	Eigen::MatrixXd positionSpace; // current matrix in position space
	Eigen::MatrixXcd momentumSpace;

	Eigen::VectorXd v_x;
	Eigen::VectorXcd v_p;

	fftw_plan x2p_vec;
	fftw_plan p2x_vec;

	fftw_plan x2p_col;
	fftw_plan p2x_col;

	fftw_plan x2p_row;
	fftw_plan p2x_row;

	double plog;

	mymeasurement<double> m_dens;
	mymeasurement<double> m_magn;

	std::string outfn;

	Eigen::MatrixXd U_s;
	Eigen::MatrixXd U_s_inv;
	Eigen::VectorXcd ev_s;

	std::vector<double> fields;
	std::vector<weighted_measurement<double>> densities;
	std::vector<weighted_measurement<double>> magnetizations;
	std::vector<weighted_measurement<double>> kinetic;
	std::vector<weighted_measurement<double>> interaction;
	std::vector<weighted_measurement<double>> spincorrelation;

	public:

	void init () {
		if (Lx<2) { Lx = 1; tx = 0.0; }
		if (Ly<2) { Ly = 1; ty = 0.0; }
		if (Lz<2) { Lz = 1; tz = 0.0; }
		V = Lx * Ly * Lz;
		randomPosition = std::uniform_int_distribution<int>(0, V-1);
		randomTime = std::uniform_int_distribution<int>(0, N-1);
		dt = beta/N;
		A = sqrt(exp(g*dt)-1.0);
		diagonals.insert(diagonals.begin(), N, Eigen::VectorXd::Zero(V));
		for (size_t i=0;i<diagonals.size();i++) {
			for (int j=0;j<V;j++) {
				diagonals[i][j] = distribution(generator)?A:-A;
			}
		}
		v_x = Eigen::VectorXd::Zero(V);
		v_p = Eigen::VectorXcd::Zero(V);
		positionSpace = Eigen::MatrixXd::Identity(V, V);
		momentumSpace = Eigen::MatrixXcd::Identity(V, V);

		const int size[] = { Lx, Ly, Lz, };
		x2p_vec = fftw_plan_dft_r2c(3, size, v_x.data(), reinterpret_cast<fftw_complex*>(v_p.data()), FFTW_PATIENT);
		p2x_vec = fftw_plan_dft_c2r(3, size, reinterpret_cast<fftw_complex*>(v_p.data()), v_x.data(), FFTW_PATIENT);
		x2p_col = fftw_plan_many_dft_r2c(3, size, V, positionSpace.data(),
				NULL, 1, V, reinterpret_cast<fftw_complex*>(momentumSpace.data()), NULL, 1, V, FFTW_PATIENT);
		p2x_col = fftw_plan_many_dft_c2r(3, size, V, reinterpret_cast<fftw_complex*>(momentumSpace.data()),
				NULL, 1, V, positionSpace.data(), NULL, 1, V, FFTW_PATIENT);
		x2p_row = fftw_plan_many_dft_r2c(3, size, V, positionSpace.data(),
				NULL, V, 1, reinterpret_cast<fftw_complex*>(momentumSpace.data()), NULL, V, 1, FFTW_PATIENT);
		p2x_row = fftw_plan_many_dft_c2r(3, size, V, reinterpret_cast<fftw_complex*>(momentumSpace.data()),
				NULL, V, 1, positionSpace.data(), NULL, V, 1, FFTW_PATIENT);

		positionSpace = Eigen::MatrixXd::Identity(V, V);
		momentumSpace = Eigen::MatrixXcd::Identity(V, V);

		energies = Eigen::VectorXd::Zero(V);
		freePropagator = Eigen::VectorXd::Zero(V);
		freePropagator_b = Eigen::VectorXd::Zero(V);
		for (int i=0;i<V;i++) {
			energies[i] += -2.0 * ( tx * cos(2.0*((i/Ly/Lz)%Lx)*pi/Lx) + ty * cos(2.0*((i/Lz)%Ly)*pi/Ly) + tz * cos(2.0*(i%Lz)*pi/Lz) );
			freePropagator[i] = exp(-dt*energies[i]);
			freePropagator_b[i] = exp(dt*energies[i]);
		}

		plog = logProbability();

		for (int i=-15;i<30;i++) {
			double f = B + double(i)/10.0;
			fields.push_back(f);
			densities.push_back(weighted_measurement<double>());
			magnetizations.push_back(weighted_measurement<double>());
			kinetic.push_back(weighted_measurement<double>());
			interaction.push_back(weighted_measurement<double>());
			spincorrelation.push_back(weighted_measurement<double>());
		}
	}

	Configuration (lua_State *L, int index, int seed = 42) : distribution(0.5), trialDistribution(1.0) {
		lua_getfield(L, index, "SEED"); generator.seed(lua_tointeger(L, -1)+seed); lua_pop(L, 1);
		lua_getfield(L, index, "Lx");   this->Lx = lua_tointeger(L, -1);           lua_pop(L, 1);
		lua_getfield(L, index, "Ly");   this->Ly = lua_tointeger(L, -1);           lua_pop(L, 1);
		lua_getfield(L, index, "Lz");   this->Lz = lua_tointeger(L, -1);           lua_pop(L, 1);
		lua_getfield(L, index, "N");    N = lua_tointeger(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "T");    beta = 1.0/lua_tonumber(L, -1);            lua_pop(L, 1);
		lua_getfield(L, index, "tx");   tx = lua_tonumber(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "ty");   ty = lua_tonumber(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "tz");   tz = lua_tonumber(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "U");    g = -lua_tonumber(L, -1);                  lua_pop(L, 1); // FIXME: check this // should be right as seen in A above
		lua_getfield(L, index, "mu");   mu = lua_tonumber(L, -1);                  lua_pop(L, 1);
		lua_getfield(L, index, "B");    B = lua_tonumber(L, -1);                   lua_pop(L, 1);
		lua_getfield(L, index, "OUTPUT");  outfn = lua_tostring(L, -1);            lua_pop(L, 1);
		init();
	}

	double logDetU_s (int end = -1) {
		end = end<0?N:end;
		int nspinup = 0;
		for (int i=0;i<end;i++) {
			for (int j=0;j<V;j++) {
				if (diagonals[i][j]>0.0) nspinup++;
			}
		}
		//std::cerr << "nspinup " << nspinup << " nspindn " << (N*V-nspinup) << std::endl;
		//std::cerr << "free_contrib = " << freePropagator.array().log().sum() << std::endl;
		return nspinup*std::log(1.0+A) + (end*V-nspinup)*std::log(1.0-A);
	}

	void accumulate_forward (int start = 0, int end = -1) {
		positionSpace.setIdentity(V, V);
		end = end<0?N:end;
		end = end>N?N:end;
		for (int i=start;i<end;i++) {
			positionSpace.applyOnTheLeft((Eigen::VectorXd::Constant(V, 1.0)+diagonals[i]).asDiagonal());
			fftw_execute(x2p_col);
			momentumSpace.applyOnTheLeft(freePropagator.asDiagonal());
			fftw_execute(p2x_col);
			positionSpace /= V;
			//std::cerr << std::log(positionSpace.determinant()) << " <> " << logDetU_s(i+1) << std::endl;
			//std::cerr << "free_contrib = " << freePropagator.array().log().sum() << std::endl;
		}
	}

	void accumulate_backward (int start = 0, int end = -1) {
		double X = 1.0 - A*A;
		positionSpace.setIdentity(V, V);
		end = end<0?N:end;
		end = end>N?N:end;
		for (int i=start;i<end;i++) {
			positionSpace.applyOnTheRight((Eigen::VectorXd::Constant(V, 1.0)-diagonals[i]).asDiagonal());
			fftw_execute(x2p_row);
			momentumSpace.applyOnTheRight(freePropagator_b.asDiagonal());
			fftw_execute(p2x_row);
			positionSpace /= V*X;
		}
	}

	double logProbability_simple () {
		double X = 1.0 - A*A;
		double Y = pow(X, N);
		accumulate_forward(0, N);
		Eigen::MatrixXd U_s = positionSpace;
		accumulate_backward(0, N);
		std::cout << std::endl;
		std::cout << N << std::endl;
		std::cout << X << std::endl;
		std::cout << Y << std::endl;
		std::cout << U_s*positionSpace << std::endl << std::endl;
		std::cout << positionSpace*U_s << std::endl << std::endl;
		std::cout << U_s.eigenvalues().transpose() << std::endl;
		std::cout << positionSpace.eigenvalues().transpose() << std::endl;
		std::cout << positionSpace.eigenvalues().array().inverse().transpose() << std::endl;
		std::cout << std::endl;
		return 0.0;
	}

	double logProbability_complex () {
		const int M = 30;
		std::vector<Eigen::MatrixXd> fvec;
		std::vector<Eigen::MatrixXd> bvec;
		for (int i=0;i<N;i+=M) {
			accumulate_forward(i, i+M);
			fvec.push_back(positionSpace);
		}
		for (int i=0;i<N;i+=M) {
			accumulate_backward(i, i+M);
			bvec.push_back(positionSpace);
		}
		test_sequences(fvec, bvec);
		return 0.0;
	}

	void sort_vector (Eigen::VectorXcd &v) {
		const int N = v.size();
		for (int i=0;i<N;i++) {
			for (int j=i+1;j<N;j++) {
				if (std::norm(v[i])<std::norm(v[j])) {
					std::complex<double> x = v[j];
					v[j] = v[i];
					v[i] = x;
				}
			}
		}
	}

	void reverse_vector (Eigen::VectorXcd &v) {
		const int N = v.size();
		for (int i=0;i<N/2;i++) {
			const int j = N-i-1;
			std::complex<double> x = v[j];
			v[j] = v[i];
			v[i] = x;
		}
	}

	double logProbability () {
		Eigen::VectorXcd eva, evb, evc;
		//dggev(positionSpace, Eigen::MatrixXd::Identity(V, V), eva, evb);
		eva = Eigen::VectorXcd::Ones(V);
		accumulate_backward();
		evc = positionSpace.eigenvalues();
		accumulate_forward();
		evb = positionSpace.eigenvalues();
		sort_vector(evb);
		sort_vector(evc);
		reverse_vector(evc);
		for (int i=0;i<V;i++) {
			if (std::norm(evb[i]/evb[0])<std::norm(evc[i]/evc[V-1])) {
				eva[i] = 1.0/evc[i];
			} else {
				eva[i] = evb[i];
			}
		}
		//std::cout << eva.transpose() << std::endl;
		//std::cout << evb.transpose() << std::endl;
		//std::cout << evc.transpose() << std::endl;

		std::complex<double> c = eva.array().log().sum();

		if ( std::cos(c.imag())<0.99 || std::abs(1.0-c.real()/logDetU_s())>1.0e-5 ) {
			std::cout << logDetU_s() << " vs. " << c << " vs. " << evb.array().log().sum() << " vs. " << evc.array().log().sum() << std::endl;
			std::cout << eva.transpose() << std::endl;
			std::cout << evb.transpose() << std::endl;
			std::cout << evc.transpose() << std::endl;
			std::cout << evc.array().inverse().transpose() << std::endl;
			std::cout << V << " " << beta*4*(tx+ty+tz) << std::endl;
			//logProbability_complex();
			throw("wtf");
		}

		std::complex<double> ret = 0.0;
		ret += (Eigen::VectorXcd::Ones(V) + std::exp(+beta*B*0.5+beta*mu)*eva).array().log().sum();
		ret += (Eigen::VectorXcd::Ones(V) + std::exp(-beta*B*0.5+beta*mu)*eva).array().log().sum();

		if (std::cos(ret.imag())<0.99) {
			//logProbability_complex();
			std::cout << logDetU_s() << " vs. " << eva.array().log().sum() << " vs. " << evb.array().log().sum() << " vs. " << evc.array().log().sum() << std::endl;
			std::cout << eva.transpose() << std::endl;
			std::cout << evc.transpose() << std::endl;
			std::cout << evc.array().inverse().transpose() << std::endl;
			std::cout << V << " " << beta*4*(tx+ty+tz) << std::endl;
			std::cout << "still wrong!" << std::endl;
			throw("wtf");
		}

		return ret.real();
	}

	Eigen::VectorXcd rank1EV_f (int x, int t, const Eigen::MatrixXd &M) {
		v_x = Eigen::VectorXd::Zero(V);
		v_x[x] = 1.0;
		for (int i=t+1;i<N;i++) {
			fftw_execute(x2p_vec);
			v_p = v_p.array() * freePropagator.array();
			fftw_execute(p2x_vec);
			v_x = v_x.array() * (Eigen::VectorXd::Constant(V, 1.0)+diagonals[i]).array();
			v_x /= V;
		}
		fftw_execute(x2p_vec);
		v_p = v_p.array() * freePropagator.array();
		fftw_execute(p2x_vec);
		v_x /= V;
		Eigen::VectorXd u = v_x;
		v_x = Eigen::VectorXd::Zero(V);
		v_x[x] = 1.0;
		for (int i=t-1;i>=0;i--) {
			fftw_execute(x2p_vec);
			v_p = v_p.array() * freePropagator.array();
			fftw_execute(p2x_vec);
			v_x = v_x.array() * (Eigen::VectorXd::Constant(V, 1.0)+diagonals[i]).array();
			v_x /= V;
		}
		Eigen::VectorXd v = v_x;
		//std::cerr << "beta:" << beta*tx << std::endl;
		//std::cerr << "u:" << std::endl << u.transpose() << std::endl;
		//std::cerr << "v:" << std::endl << v.transpose() << std::endl;
		//accumulate_forward();
		//std::cerr << "original:" << std::endl << positionSpace << std::endl;
		//std::cerr << "rank-1:" << std::endl << positionSpace-2*diagonals[t][x]*u*v.transpose() << std::endl;
		//diagonals[t][x] = -diagonals[t][x];
		//accumulate_forward();
		//std::cerr << "plain" << std::endl << positionSpace << std::endl << std::endl;
		return (M-2*diagonals[t][x]*u*v.transpose()).eigenvalues();
	}

	Eigen::VectorXcd rank1EV_b (int x, int t, const Eigen::MatrixXd &M) {
		double X = 1-A*A;
		v_x = Eigen::VectorXd::Zero(V);
		v_x[x] = 1.0;
		for (int i=t+1;i<N;i++) {
			fftw_execute(x2p_vec);
			v_p = v_p.array() * freePropagator_b.array();
			fftw_execute(p2x_vec);
			v_x = v_x.array() * (Eigen::VectorXd::Constant(V, 1.0)-diagonals[i]).array();
			v_x /= V*X;
		}
		fftw_execute(x2p_vec);
		v_p = v_p.array() * freePropagator_b.array();
		fftw_execute(p2x_vec);
		v_x /= V;
		Eigen::VectorXd u = v_x;
		v_x = Eigen::VectorXd::Zero(V);
		v_x[x] = 1.0;
		for (int i=t-1;i>=0;i--) {
			fftw_execute(x2p_vec);
			v_p = v_p.array() * freePropagator_b.array();
			fftw_execute(p2x_vec);
			v_x = v_x.array() * (Eigen::VectorXd::Constant(V, 1.0)-diagonals[i]).array();
			v_x /= V*X;
		}
		Eigen::VectorXd v = v_x;
		//std::cerr << "beta:" << beta*tx << std::endl;
		//std::cerr << "u:" << std::endl << u.transpose() << std::endl;
		//std::cerr << "v:" << std::endl << v.transpose() << std::endl;
		//std::cerr << "original:" << std::endl << M << std::endl;
		//std::cerr << "rank-1:" << std::endl << M+2*diagonals[t][x]*v*u.transpose()/X << std::endl;
		//diagonals[t][x] = -diagonals[t][x];
		//accumulate_backward();
		//std::cerr << "plain" << std::endl << M << std::endl << std::endl;
		//throw("end");
		return (M+2*diagonals[t][x]*v*u.transpose()/X).eigenvalues();
	}

	double rank1prob (int x, int t) {
		Eigen::VectorXcd eva = Eigen::VectorXcd::Ones(V);
		accumulate_forward();
		Eigen::VectorXcd evb = rank1EV_f(x, t, positionSpace);
		accumulate_backward();
		Eigen::VectorXcd evc = rank1EV_b(x, t, positionSpace);
		sort_vector(evb);
		sort_vector(evc);
		reverse_vector(evc);
		for (int i=0;i<V;i++) {
			if (std::norm(evb[i]/evb[0])<std::norm(evc[i]/evc[V-1])) {
				eva[i] = 1.0/evc[i];
			} else {
				eva[i] = evb[i];
			}
		}
		//std::cout << eva.transpose() << std::endl;
		//std::cout << evb.transpose() << std::endl;
		//std::cout << evc.transpose() << std::endl;
		std::complex<double> ret = 0.0;
		ret += (Eigen::VectorXcd::Ones(V) + std::exp(+beta*B*0.5+beta*mu)*eva).array().log().sum();
		ret += (Eigen::VectorXcd::Ones(V) + std::exp(-beta*B*0.5+beta*mu)*eva).array().log().sum();
		return ret.real();
	}

	bool metropolis (int M = 0) {
		bool ret = false;
		int x = randomPosition(generator);
		int t = randomTime(generator);
		//double trial1 = rank1prob(x, t);
		diagonals[t][x] = -diagonals[t][x];
		double trial = logProbability();
		//logProbability_complex();
		//throw "end";
		//std::cerr << trial << " " << trial1 << std::endl;
		if (-trialDistribution(generator)<trial-plog) {
			plog = trial;
			U_s = positionSpace;
			ev_s = positionSpace.eigenvalues();
			ret = true;
		} else {
			diagonals[t][x] = -diagonals[t][x];
			ret = false;
		}
		return ret;
	}

	double fraction_completed () const {
		return 1.0;
	}

	void update () {
		metropolis();
	}

	void extract_data (const Eigen::MatrixXd &M, Eigen::ArrayXd &d, Eigen::ArrayXd &d1, Eigen::ArrayXd &d2, double &K) {
		positionSpace = M;
		d = positionSpace.diagonal();
		d1.resize(positionSpace.rows());
		d2.resize(positionSpace.rows());
		// get super- and sub- diagonal
		for (int i=0;i<V;i++) {
			d1[i] = positionSpace(i, (i+1)%V);
			d2[i] = positionSpace((i+1)%V, i);
		}
		fftw_execute(x2p_col);
		momentumSpace.applyOnTheLeft(energies.asDiagonal());
		fftw_execute(p2x_col);
		K = positionSpace.trace() / V;
	}

	void measure () {
		Eigen::ArrayXd d_up, d_dn;
		Eigen::ArrayXd d1_up, d1_dn;
		Eigen::ArrayXd d2_up, d2_dn;
		double K_up, K_dn;
		double n_up, n_dn, n2;
		//extract_data(Eigen::MatrixXd::Identity(V, V) - (Eigen::MatrixXd::Identity(V, V) + exp(+beta*B*0.5+beta*mu)*U_s).inverse(), d_up, d1_up, d2_up, K_up);
		//extract_data(Eigen::MatrixXd::Identity(V, V) - (Eigen::MatrixXd::Identity(V, V) + exp(-beta*B*0.5+beta*mu)*U_s).inverse(), d_dn, d1_dn, d2_dn, K_dn);
		//n_up = d_up.sum();
		//n_dn = d_dn.sum();
		//n2 = (d_up*d_dn).sum();
		//if (std::isnan(n_up) || std::isinf(n_up)) {
			//throw(9);
		//}
		//m_dens.add( (n_up + n_dn) / V );
		//m_magn.add( (n_up - n_dn) / 2.0 / V );
		for (size_t i=0;i<fields.size();i++) {
			double B = fields[i];
			std::complex<double> ret = 0.0;
			ret += (1.0 + std::exp(+beta*B*0.5+beta*mu)*ev_s.array()).log().sum();
			ret += (1.0 + std::exp(-beta*B*0.5+beta*mu)*ev_s.array()).log().sum();
			double w = std::exp(ret-plog).real();
			extract_data(Eigen::MatrixXd::Identity(V, V) - (Eigen::MatrixXd::Identity(V, V) + exp(+beta*B*0.5+beta*mu)*U_s).inverse(), d_up, d1_up, d2_up, K_up);
			extract_data(Eigen::MatrixXd::Identity(V, V) - (Eigen::MatrixXd::Identity(V, V) + exp(-beta*B*0.5+beta*mu)*U_s).inverse(), d_dn, d1_dn, d2_dn, K_dn);
			n_up = d_up.sum();
			n_dn = d_dn.sum();
			n2 = (d_up*d_dn).sum();
			if (std::cos(ret.imag())<0.99 && std::cos(ret.imag())>0.01) {
				//throw 1;
			}
			densities[i].add((n_up + n_dn) / V, w);
			magnetizations[i].add((n_up - n_dn) / 2.0 / V, w);
			kinetic[i].add(K_up+K_dn, w);
			interaction[i].add(g*(n_up-n2), w);
			double ssz = - (d1_up*d2_up).sum() - (d1_dn*d2_dn).sum() - 2.0*n_up - 2.0*n_dn;
			for (int j=0;j<V;j++) {
				ssz += d_up[j]*d_up[(j+1)%V] + d_dn[j]*d_dn[(j+1)%V];
				ssz += d_up[j]*d_dn[(j+1)%V] + d_dn[j]*d_up[(j+1)%V];
			}
			spincorrelation[i].add(0.25*ssz, w);
		}
	}

	int volume () { return V; }
	int timeSlices () { return N; }

	void output_results () {
		std::ofstream out (outfn, std::ios::app);
		out << "# T mu N \\Delta N^2 M \\Delta M^2" << std::endl;
		for (size_t i=0;i<fields.size();i++) {
			out << 1.0/(beta*tx) << ' ' << 0.5*(fields[i]+g)/tx
				<< ' ' << 1+2*(magnetizations[i].mean()) << ' ' << 4*magnetizations[i].variance()
				<< ' ' << 0.5*(densities[i].mean()-1.0) << ' ' << 0.25*densities[i].variance()
				<< ' ' << kinetic[i].mean()/tx/V << ' ' << kinetic[i].variance()
				<< ' ' << interaction[i].mean()/tx/V << ' ' << interaction[i].variance()
				<< ' ' << spincorrelation[i].mean()/V << ' ' << spincorrelation[i].variance() << std::endl;
		}
		out << std::endl;
	}

	~Configuration () {
		fftw_destroy_plan(x2p_vec);
		fftw_destroy_plan(p2x_vec);
		fftw_destroy_plan(x2p_col);
		fftw_destroy_plan(p2x_col);
		fftw_destroy_plan(x2p_row);
		fftw_destroy_plan(p2x_row);
	}
	protected:
};

using namespace std;

int main (int argc, char **argv) {
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	luaL_dofile(L, argv[1]);

	for (int i=1;i<=lua_gettop(L);i++) {
		lua_getfield(L, i, "THREADS");
		int nthreads = lua_tointeger(L ,-1);
		lua_pop(L, 1);
		lua_getfield(L, i, "THERMALIZATION");
		int thermalization_sweeps = lua_tointeger(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, i, "SWEEPS");
		int total_sweeps = lua_tointeger(L, -1);
		lua_pop(L, 1);
		std::vector<std::thread> threads(nthreads);
		std::mutex lock;
		for (int j=0;j<nthreads;j++) {
			threads[j] = std::thread( [=,&lock] () {
					try {
					lock.lock();
					Configuration configuration(L, i, j);
					lock.unlock();

					int n = 0;
					int a = 0;
					int M = 1;
					for (int i=0;i<thermalization_sweeps;i++) {
					if (i%100==0) { std::cout << i << "\r"; std::cout.flush(); }
					if (configuration.metropolis(1)) a++;
					n++;
					}
					std::cout << thermalization_sweeps << "\n"; std::cout.flush();
					for (int i=0;i<total_sweeps;i++) {
						if (i%100==0) { std::cout << i << "\r"; std::cout.flush(); }
						if (configuration.metropolis(1)) a++;
						n++;
						configuration.measure();
					}
					std::cout << total_sweeps << "\n"; std::cout.flush();
					lock.lock();
					configuration.output_results();
					lock.unlock();
					} catch (...) { std::cerr << "caught exception in main() " << std::endl; }
			});
		}
		for (std::thread& t : threads) t.join();
	}

	lua_close(L);
	return 0;
}

