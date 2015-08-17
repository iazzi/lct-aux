#ifndef LCTSIMULATION
#define LCTSIMULATION

#include "configuration2.hpp"
#include "genericlattice.hpp"
#include "slice.hpp"
#include "model.hpp"
#include "hubbard.hpp"
#include "spin_one_half.hpp"

#include <random>

class LCTSimulation {
	std::mt19937_64 generator;
	std::uniform_real_distribution<double> d;
	std::exponential_distribution<double> trial;
	SpinOneHalf<GenericLattice> lattice;
	HubbardInteraction interaction;
	Model<SpinOneHalf<GenericLattice>, HubbardInteraction> model;
	Configuration2<Model<SpinOneHalf<GenericLattice>, HubbardInteraction>> conf;
	double p1; // probability at the start of the simulation (absolute value)
	double pr; // probability ration of the current configuration wrt p1 (absolute values)
	double ps; // sign of the current configuration
	public:
	LCTSimulation (Parameters params) :
		lattice(params),
		interaction(params),
		model(lattice, interaction),
		conf(model) {
			conf.setup(params);
			for (size_t i=0;i<conf.slice_number();i++) {
				conf.set_index(i);
				for (size_t j=0;j<2*lattice.volume();j++) {
					conf.insert(interaction.generate(0.0, conf.slice_end()-conf.slice_start(), generator));
				}
				//std::cerr << i << " -> " << conf.slice_size() << std::endl;
			}
			conf.set_index(0);
			conf.compute_right_side(0);
			conf.start();
			conf.start();
			conf.compute_B();
			conf.compute_G();
			conf.save_G();
			p1 = 0.0, ps = 0.0, pr = 0.0;
			std::tie(p1, ps) = conf.probability();
		}

	void update (bool check = false) {
		HubbardInteraction::Vertex v;
		double dp = 0.0, s = 1.0;
		if (d(generator)<0.5) {
			v = conf.get_vertex(d(generator)*conf.slice_size());
			dp = conf.remove_probability(v);
			s = dp>0.0?1.0:-1.0;
			dp = std::log(std::fabs(dp));
			if (-trial(generator)<dp+conf.remove_factor()) {
				//cerr << "removed vertex " << v.tau << endl;
				conf.remove_and_update(v);
				pr += dp;
				ps *= s;
			} else {
				//cerr << "remove rejected" << endl;
			}
		} else {
			v = model.interaction().generate(0.0, conf.slice_end()-conf.slice_start(), generator);
			dp = conf.insert_probability(v);
			s = dp>0.0?1.0:-1.0;
			dp = std::log(std::fabs(dp));
			if (-trial(generator)<dp+conf.insert_factor()) {
				//cerr << "inserted vertex " << v.tau << endl;
				conf.insert_and_update(v);
				pr += dp;
				ps *= s;
			} else {
				//cerr << "insert rejected" << endl;
			}
		}
		if (check) {
			conf.compute_B();
			double p2 = conf.probability().first;
			//std::cerr << "v = " << v.x << ',' << v.tau << " dp = " << p1+pr-p2 << ' ' << p2-p1 << ' ' << pr << std::endl << endl;
		}
		//conf.compute_right_side(conf.current_slice()+1);
	}

	void sweep (bool check = false) {
		HubbardInteraction::Vertex v;
		for (size_t j=0;j<model.lattice().volume();j++) {
			update(check);
		}
		//conf.compute_right_side(conf.current_slice()+1);
	}

	void full_sweep (bool check = false) {
		Eigen::MatrixXd G;
		for (size_t i=0;i<conf.slice_number();i++) {
			conf.set_index(i);
			conf.compute_right_side(conf.current_slice()+1);
			//conf.compute_B();
			//conf.compute_G();
			//conf.save_G();
			//G = conf.green_function();
			conf.compute_propagators_2();
			//std::cerr << G << std::endl << std::endl;
			//cerr << (double(i)/conf.slice_number()) << ' '
				//<< conf.inverse_temperature() << ' '
				//<< model.interaction().dimension() << ' '
				//<< (conf.green_function()-G).norm() << ' '
				//<< (conf.green_function()-G).cwiseAbs().maxCoeff() << endl;
			sweep(check);
			conf.compute_right_side(conf.current_slice()+1);
		}
		for (size_t i=conf.slice_number();i>0;i--) {
			conf.set_index(i-1);
			conf.compute_left_side(conf.current_slice()+1);
			//std::cerr << G << std::endl << std::endl;
			//conf.compute_B();
			//conf.compute_G();
			//conf.save_G();
			conf.compute_propagators_2();
			//G = conf.green_function();
			//cerr << (double(i-1)/conf.slice_number()) << ' '
			//<< conf.inverse_temperature() << ' '
			//<< model.interaction().dimension() << ' '
			//<< (conf.green_function()-G).norm() << ' '
			//<< (conf.green_function()-G).cwiseAbs().maxCoeff() << endl;
			sweep(check);
		}
		if (check) {
			conf.start();
			conf.check_all_prop();
			//conf.check_all_det(0);
			//conf.check_all_det(1);
		}
	}

	double probability () const { return p1+pr; }
	double sign () const { return ps; }

	size_t vertices () const { return conf.vertices(); }
	const Eigen::MatrixXd & green_function () const {
		return conf.green_function();
	}

	double kinetic_energy (const Eigen::MatrixXd& cache) const {
		return model.lattice().kinetic_energy(cache);
	}

	double interaction_energy (const Eigen::MatrixXd& cache) const {
		return model.interaction().interaction_energy(cache);
	}

	size_t volume () const { return model.lattice().volume(); }
};

#endif // LCTSIMULATION
