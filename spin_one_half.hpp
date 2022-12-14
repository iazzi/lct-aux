#ifndef SPIN_ONE_HALF_HPP
#define SPIN_ONE_HALF_HPP

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

template <typename Lattice>
class SpinOneHalf {
	size_t V;

	Eigen::VectorXd eigenvalues_;
	Eigen::MatrixXd eigenvectors_;

	Lattice l_;

	bool computed;

	public:

	void setup (const Parameters &p) {
		l_.setup(p);
		computed = false;
	}

	void compute () {
		if (computed) return;
		l_.compute();
		V = l_.dimension();
		eigenvectors_ = Eigen::MatrixXd::Zero(2*V, 2*V);
		eigenvectors_.block(0, 0, V, V) = l_.eigenvectors();
		eigenvectors_.block(V, V, V, V) = l_.eigenvectors();
		eigenvalues_.setZero(2*V);
		eigenvalues_.head(V) = l_.eigenvalues();
		eigenvalues_.tail(V) = l_.eigenvalues();
		computed = true;
	}

	const Eigen::VectorXd & eigenvalues () const { return eigenvalues_; }
	const Eigen::MatrixXd & eigenvectors () const { return eigenvectors_; }

	size_t volume () const { return V; }
	size_t states () const { return 2*V; }
	size_t dimension () const { return 2*V; }

	SpinOneHalf (): computed(false) { compute(); }
	SpinOneHalf (const Lattice &l): l_(l), computed(false) { compute(); }
	SpinOneHalf (const Parameters &p): l_(p), computed(false) { compute(); }
};



#endif // SPIN_ONE_HALF_HPP

