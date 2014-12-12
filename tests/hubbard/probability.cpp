#include "configuration.hpp"
#include "cubiclattice.hpp"
#include "slice.hpp"
#include "model.hpp"
#include "hubbard.hpp"

#include <random>
#include <iostream>
#include <Eigen/Dense>

#include <algorithm>

using namespace std;
using namespace Eigen;

const int L = 10;

double relative_error (double a, double b) {
	return fabs(a-b)/min(fabs(a), fabs(b));
}

int main () {
	std::mt19937_64 generator;
	CubicLattice lattice;
	lattice.set_size(L, L, 1);
	lattice.compute();
	HubbardInteraction interaction(generator);
	interaction.setup(lattice.eigenvectors(), 4.0, 5.0);
	auto model = make_model(lattice, interaction);
	Configuration<Model<CubicLattice, HubbardInteraction>> conf(generator, model);
	conf.setup(20.0, 0.0, 80); // beta, mu (relative to half filling), slice number
	for (int i=0;i<20*L*L;i++) {
		conf.insert(interaction.generate(0.0, 20.0));
	}
	for (int i=0;i<40;i++) {
		conf.set_index(i);
		conf.compute_B();
		if (relative_error(conf.log_abs_det(), conf.slice_log_abs_det())>1e-8 && conf.log_abs_det()>1.0e-8) {
			cerr << relative_error(conf.log_abs_det(), conf.slice_log_abs_det()) << endl;
			cerr << conf.log_abs_det() << ' ' << conf.slice_log_abs_det() << endl;
			cerr << conf.log_abs_max() << endl;
			return 1;
		}
		cerr << conf.log_abs_det() << " " << conf.slice_log_abs_det() << endl;
		conf.compute_G();
		conf.save_G();
		HubbardInteraction::Vertex v = interaction.generate(conf.slice_start(), conf.slice_end());
		double p1 = conf.probability().first;
		double pr = conf.insert_probability(v);
		conf.insert_and_update(v);
		conf.compute_B();
		conf.compute_G();
		cerr << conf.check_and_save_G() << endl;
		double p2 = conf.probability().first;
		std::cerr << std::log(pr)-p2+p1 << ' ' << v.tau-conf.slice_start() << ' ' << conf.slice_start() << ' ' << conf.slice_size() << endl;
	}
	return 0;
}



