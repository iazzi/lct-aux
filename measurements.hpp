#ifndef __MEASUREMENTS_HPP
#define __MEASUREMENTS_HPP

#include <vector>
#include <iostream>

#include <cmath>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

template <typename T, bool Log = false>
class measurement {
	private:
		std::vector<T> sums_;
		std::vector<T> squared_sums_;
		std::vector<T> x_;
		std::vector<int> n_;
		std::string name_;
	public:
		const std::string &name () const { return name_; }
		void set_name (const std::string &name) { name_ = name; }
		void set_bins (int b) {
			b = b>=0?b:0;
			sums_.resize(b);
			squared_sums_.resize(b);
			x_.resize(b);
			n_.resize(b);
		}
		void set_last_value (int i, const T &x) { x_[i] = x; }
		void set_sum (int i, const T &x) { sums_[i] = x; }
		void set_squared_sum (int i, const T &x) { squared_sums_[i] = x; }
		void set_samples (int i, int n) { n_[i] = n; }

		void clear () { set_bins(0); }

		template <typename U>
		void add (U &&x) {
			T nx = std::forward<U>(x);
			for (size_t i=0;;i++) {
				if (i==n_.size()) {
					sums_.push_back(T());
					squared_sums_.push_back(T());
					x_.push_back(T());
					n_.push_back(0);
					if (Log) break;
				}
				if (n_[i]==0) {
					sums_[i] = nx;
					squared_sums_[i] = nx * nx;
				} else {
					sums_[i] += nx;
					squared_sums_[i] += nx * nx;
				}
				n_[i] += 1;
				if (n_[i]%2==1) {
					x_[i] = nx;
					break;
				} else {
					nx = (nx + x_[i]) / 2.0;
					x_[i] = nx;
				}
			}
		}

		void repeat () { add(x_[0]); }

		T last_value (int i = 0) const { return x_[i]; }
		T sum (int i = 0) const { return sums_[i]; }
		T mean (int i = 0) const { if (bins()>0) return sums_[i] / double(n_[i]); else return T(); }
		T square (int i = 0) const { return squared_sums_[i]; }

		T variance (int i = 0) const {
			T m = mean(i);
			T m2 = squared_sums_[i] / double(n_[i]);
			return m2 - m*m;
		}

		T error (int i) const {
			if (bins()>0)
				return sqrt( variance(i) / double(n_[i]) );
			else
				return T();
		}

		T error () const {
			return error(std::max(size_t(0), bins()-6));
		}

		size_t bins() const { return n_.size(); }
		int samples (int i = 0) const { if (n_.size()==0) return 0; else return n_[i]; }

		double time (int i) const {
			if  (bins()>0)
				return (variance(i)*n_[0]/n_[i]/variance(0)-1.0)*0.5;
			else
				return 0.0;
		}

		double time () const {
			return time(std::max(bins(), size_t(6))-6);
		}

		measurement () : name_("Result") {}

	protected:
};

template <typename T, bool Log> std::ostream& operator<< (std::ostream& out, const measurement<T, Log>& m) {
	if (m.samples()==0) {
		out << m.name() << ": Empty." << std::endl;
	} else {
		int N = std::max(int(m.bins())-6, int(0));
		out << m.name() << ": " << m.mean() << " +- " << m.error(N) << std::endl;
		if (N<2 || 2*m.error(N-1)<(m.error(N)+m.error(N-2))) {
			out << "NOT CONVERGING" << std::endl;
		}
		out << "Bins: " << m.bins() << std::endl;
		for (size_t i=0;i<m.bins();i++) {
			out << (m.samples(i)>100?'#':'?') << i+1 << ": samples = " << m.samples(i) << ", value = " << m.mean(i) << " +- " << m.error(i) << ", autocorrelation time = " << m.time(i) << std::endl;
		}
	}
	return out;
}

template <typename T, bool Log> lua_State* operator<<= (lua_State *L, const measurement<T, Log>& m) {
	int t = lua_gettop(L);
	lua_pushlstring(L, m.name().c_str(), m.name().length());
	lua_setfield(L, t, "name");
	lua_pushinteger(L, m.bins());
	lua_setfield(L, t, "bins");
	lua_newtable(L);
	for (size_t i=0;i<m.bins();i++) {
		lua_pushinteger(L, m.samples(i));
		lua_rawseti(L, -2, i+1);
	}
	lua_setfield(L, t, "samples");
	lua_newtable(L);
	for (size_t i=0;i<m.bins();i++) {
		lua_pushnumber(L, m.sum(i));
		lua_rawseti(L, -2, i+1);
	}
	lua_setfield(L, t, "sums");
	lua_newtable(L);
	for (size_t i=0;i<m.bins();i++) {
		lua_pushnumber(L, m.square(i));
		lua_rawseti(L, -2, i+1);
	}
	lua_setfield(L, t, "squares");
	lua_newtable(L);
	for (size_t i=0;i<m.bins();i++) {
		lua_pushnumber(L, m.last_value(i));
		lua_rawseti(L, -2, i+1);
	}
	lua_setfield(L, t, "values");
	return L;
}

template <typename T, bool Log> lua_State* operator<< (lua_State *L, const measurement<T, Log>& m) {
	lua_newtable(L);
	return L <<= m;
}

template <typename T, bool Log> lua_State* operator>>= (lua_State *L, measurement<T, Log>& m) {
	int t = lua_gettop(L);
	lua_getfield(L, t, "name");
	m.set_name(lua_tostring(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, t, "bins");
	int b = lua_tointeger(L, -1);
	m.set_bins(b);
	lua_pop(L, 1);
	lua_getfield(L, t, "samples");
	for (int i=0;i<b;i++) {
		lua_rawgeti(L, -1, i+1);
		m.set_samples(i, lua_tointeger(L, -1));
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	lua_getfield(L, t, "sums");
	for (int i=0;i<b;i++) {
		lua_rawgeti(L, -1, i+1);
		m.set_sum(i, lua_tonumber(L, -1));
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	lua_getfield(L, t, "squares");
	for (int i=0;i<b;i++) {
		lua_rawgeti(L, -1, i+1);
		m.set_squared_sum(i, lua_tonumber(L, -1));
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	lua_getfield(L, t, "values");
	for (int i=0;i<b;i++) {
		lua_rawgeti(L, -1, i+1);
		m.set_last_value(i, lua_tonumber(L, -1));
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return L;
}

template <typename T, bool Log> lua_State* operator>> (lua_State *L, measurement<T, Log>& m) {
	L >>= m;
	lua_pop(L, 1);
	return L;
}

template <typename T, bool Log>
void lua_set (lua_State *L, measurement<T, Log> m) {
	L << m;
}

template <typename T, bool Log>
void lua_get (lua_State *L, measurement<T, Log> &m) {
	L >>= m;
}


#endif // __MEASUREMENTS_HPP

