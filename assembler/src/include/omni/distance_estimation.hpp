//***************************************************************************
//* Copyright (c) 2011-2012 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

#ifndef DISTANCE_ESTIMATION_HPP_
#define DISTANCE_ESTIMATION_HPP_

#include "xmath.h"
#include "paired_info.hpp"
#include "omni_utils.hpp"

#include "openmp_wrapper.h"

namespace omnigraph {

template<class Graph>
class GraphDistanceFinder {
	typedef typename Graph::EdgeId EdgeId;
    typedef vector<EdgeId> Path;

	const Graph &graph_;
	const size_t insert_size_;
	const int gap_;
	const size_t delta_;
public:
	GraphDistanceFinder(const Graph &graph, size_t insert_size,
			size_t read_length, size_t delta) :
			graph_(graph), insert_size_(insert_size), gap_(
					(int) insert_size - 2 * read_length), delta_(delta) {
	}

	const vector<size_t> GetGraphDistancesLengths(EdgeId first, EdgeId second) const {
		DifferentDistancesCallback<Graph> callback(graph_);

		PathProcessor<Graph> path_processor(
				graph_,
				omnigraph::PairInfoPathLengthLowerBound(graph_.k(),
						graph_.length(first), graph_.length(second), gap_,
						delta_),
				omnigraph::PairInfoPathLengthUpperBound(graph_.k(),
						insert_size_, delta_), graph_.EdgeEnd(first),
				graph_.EdgeStart(second), callback);
		path_processor.Process();

		vector<size_t> result = callback.distances();
		for (size_t i = 0; i < result.size(); i++) {
			result[i] += graph_.length(first);
		}
		if (first == second) {
			result.push_back(0);
		}
		sort(result.begin(), result.end());
		return result;
	}

	const vector<Path> GetGraphDistances(EdgeId first, EdgeId second) const {
		PathStorageCallback<Graph> callback(graph_);

		PathProcessor<Graph> path_processor(
				graph_,
				omnigraph::PairInfoPathLengthLowerBound(graph_.k(),
						graph_.length(first), graph_.length(second), gap_,
						delta_),
				omnigraph::PairInfoPathLengthUpperBound(graph_.k(),
						insert_size_, delta_), graph_.EdgeEnd(first),
				graph_.EdgeStart(second), callback);
		path_processor.Process();

		return callback.paths();
	}

};

template<class Graph>
class AbstractDistanceEstimator {
	typedef typename Graph::EdgeId EdgeId;
    const Graph &graph_;
	const PairedInfoIndex<Graph>& histogram_;
	const GraphDistanceFinder<Graph>& distance_finder_;
	const size_t linkage_distance_;
protected:
	AbstractDistanceEstimator(const Graph &graph,
			const PairedInfoIndex<Graph>& histogram,
			const GraphDistanceFinder<Graph>& distance_finder,
			size_t linkage_distance = 0) :
			graph_(graph), histogram_(histogram), distance_finder_(
					distance_finder), linkage_distance_(linkage_distance) {
	}

	const Graph& graph() const {
		return graph_;
	}

	const PairedInfoIndex<Graph>& histogram() const {
		return histogram_;
	}

	const vector<size_t> GetGraphDistancesLengths(EdgeId first, EdgeId second) const {
		return distance_finder_.GetGraphDistancesLengths(first, second);
	}

	vector<PairInfo<EdgeId> > ClusterResult(EdgeId edge1, EdgeId edge2,
			const vector<pair<int, double> >& estimated) const {

		vector<PairInfo<EdgeId>> result;
		for (size_t i = 0; i < estimated.size(); i++) {
			size_t left = i;
			double weight = estimated[i].second;
			while (i + 1 < estimated.size()
					&& (estimated[i + 1].first - estimated[i].first)
							<= (int) linkage_distance_) {
				i++;
				weight += estimated[i].second;
			}
			double center = (estimated[left].first + estimated[i].first) * 0.5;
			double var = (estimated[i].first - estimated[left].first) * 0.5;
			PairInfo<EdgeId> new_info(edge1, edge2, center, weight, var);
			result.push_back(new_info);
		}
		return result;
	}

	void AddToResult(PairedInfoIndex<Graph> &result,
			const vector<PairInfo<EdgeId> >& clustered) const {
		for (auto it = clustered.begin(); it != clustered.end(); ++it) {
			result.AddPairInfo(*it);
		}
	}

public:
	virtual ~AbstractDistanceEstimator() {
	}

	virtual void Estimate(PairedInfoIndex<Graph> &result) const = 0;

    virtual void EstimateParallel(PairedInfoIndex<Graph> &result, size_t nthreads) const = 0;
};


template<class Graph>
class DistanceEstimator: public AbstractDistanceEstimator<Graph> {
	typedef AbstractDistanceEstimator<Graph> base;
	typedef typename Graph::EdgeId EdgeId;
    typedef map<int, double> hist_type;

protected:
	const size_t max_distance_;

    virtual void GetHistogram(const vector<PairInfo<EdgeId>>& data, hist_type& hist) const {
        for (auto iter = data.begin(); iter != data.end(); ++iter) {
            hist.insert(make_pair(iter->d, iter->weight));
        }
    }

    virtual void ConvoluteWithIsHist(const boost::function<double(int)>& weight_f, hist_type& old_hist, hist_type& new_hist) const {
        int low_val = old_hist.begin()->first;
        int high_val = old_hist.rbegin()->first;
        DEBUG("Low value is " << low_val << " high value is " << high_val);
        VERIFY(low_val <= high_val);
        
        for (int i = low_val - 20; i < high_val + 20; ++i) {
            new_hist[i] = 0.;
            for (auto iter = old_hist.begin(); iter != old_hist.end(); ++iter) {
                new_hist[i] += iter->second * weight_f(i - iter->first);
            }
        }
    }

	virtual vector<pair<int, double>> EstimateEdgePairDistances(EdgeId first, EdgeId second,
			const vector<PairInfo<EdgeId>>& data,
			const vector<size_t>& raw_forward) const {
        size_t first_len = this->graph().length(first);
        size_t second_len = this->graph().length(second);
		vector<pair<int, double>> result;
		int maxD = rounded_d(data.back());
		int minD = rounded_d(data.front());
		vector<size_t> forward;
		for (size_t i = 0; i < raw_forward.size(); ++i)
			if (minD - (int) max_distance_ <= (int) raw_forward[i] && (int) raw_forward[i] <= maxD + (int) max_distance_)
				forward.push_back(raw_forward[i]);
		if (forward.size() == 0)
			return result;
		size_t cur_dist = 0;
		vector<double> weights(forward.size());
		for (size_t i = 0; i < data.size(); i++) {
            if (math::ls(2. * data[i].d + second_len, (double) first_len))
                continue;
			while (cur_dist + 1 < forward.size()
					&& forward[cur_dist + 1] < data[i].d) {
				cur_dist++;
			}
			if (cur_dist + 1 < forward.size()
					&& math::ls(forward[cur_dist + 1] - data[i].d,
							data[i].d - (int) forward[cur_dist])) {
				cur_dist++;
				if (math::le(std::abs(forward[cur_dist] - data[i].d), (double) max_distance_))
					weights[cur_dist] += data[i].weight;
			} else if (cur_dist + 1 < forward.size()
					&& math::eq(forward[cur_dist + 1] - data[i].d,
							data[i].d - (int) forward[cur_dist])) {
				if (math::le(std::abs(forward[cur_dist] - data[i].d), (double) max_distance_))
					weights[cur_dist] += data[i].weight * 0.5;
				cur_dist++;
				if (math::le(std::abs(forward[cur_dist] - data[i].d), (double) max_distance_))
					weights[cur_dist] += data[i].weight * 0.5;
			} else {
				if (math::le(std::abs(forward[cur_dist] - data[i].d), (double) max_distance_))
					weights[cur_dist] += data[i].weight;
			}
		}
		for (size_t i = 0; i < forward.size(); i++) {
			if (math::gr(weights[i], 0.)) {
				result.push_back(make_pair(forward[i], weights[i]));
			}
		}
		return result;
	}

	pair<EdgeId, EdgeId> ConjugatePair(EdgeId first, EdgeId second) const {
		return make_pair(this->graph().conjugate(second), this->graph().conjugate(first));
	}

	PairInfo<EdgeId> ConjugateInfo(const PairInfo<EdgeId>& pair_info) const {
		return PairInfo<EdgeId>(
				this->graph().conjugate(pair_info.second),
				this->graph().conjugate(pair_info.first),
				pair_info.d + this->graph().length(pair_info.second)
						- this->graph().length(pair_info.first),
				pair_info.weight, pair_info.variance);
	}

	vector<PairInfo<EdgeId>> ConjugateInfos(const vector<PairInfo<EdgeId>>& data) const {
		vector<PairInfo<EdgeId>> answer;
		for (auto it = data.begin(); it != data.end(); ++it) {
			answer.push_back(ConjugateInfo(*it));
		}
		return answer;
	}

	virtual void ProcessEdgePair(EdgeId first, EdgeId second, const vector<PairInfo<EdgeId>>& data, PairedInfoIndex<Graph> &result) const {
		if (make_pair(first, second) <= ConjugatePair(first, second)) {
			const vector<size_t>& forward = this->GetGraphDistancesLengths(first, second);
			const vector<pair<int, double> >& estimated = EstimateEdgePairDistances(first, second, data, forward);
			const vector<PairInfo<EdgeId>>& res = this->ClusterResult(first, second, estimated);
			this->AddToResult(result, res);
			this->AddToResult(result, ConjugateInfos(res));
		}
	}

public:
	DistanceEstimator(const Graph &graph,
			const PairedInfoIndex<Graph>& histogram,
			const GraphDistanceFinder<Graph>& distance_finder, 
			size_t linkage_distance, size_t max_distance) :
			base(graph, histogram, distance_finder, linkage_distance), max_distance_(
					max_distance) {
	}

	virtual ~DistanceEstimator() {
	}

	virtual void Estimate(PairedInfoIndex<Graph> &result) const {
		for (auto it = this->histogram().begin();
				it != this->histogram().end(); ++it) {
			ProcessEdgePair(it.first(), it.second(), *it, result);
		}
	}

    virtual void EstimateParallel(PairedInfoIndex<Graph> &result, size_t nthreads) const {
        vector< pair<EdgeId, EdgeId> > edge_pairs;

        INFO("Collecting edge pairs");

        for (auto iterator = this->histogram().begin();
                iterator != this->histogram().end(); ++iterator) {

            edge_pairs.push_back(make_pair(iterator.first(), iterator.second()));
        }

        vector< PairedInfoIndex<Graph>* > buffer(nthreads);
        buffer[0] = &result;
        for (size_t i = 1; i < nthreads; ++i) {
            buffer[i] = new PairedInfoIndex<Graph>(this->graph());
        }

        INFO("Processing");
        #pragma omp parallel num_threads(nthreads)
        {
            #pragma omp for
            for (size_t i = 0; i < edge_pairs.size(); ++i)
            {
                EdgeId first = edge_pairs[i].first;
                EdgeId second = edge_pairs[i].second;
                ProcessEdgePair(first, second, this->histogram().GetEdgePairInfo(first, second), *buffer[omp_get_thread_num()]);
            }
        }

        INFO("Merging maps");
        for (size_t i = 1; i < nthreads; ++i) {
            buffer[0]->AddAll(*(buffer[i]));
            delete buffer[i];
        }
    }

private:
    DECL_LOGGER("DistanceEstimator");
};

template<class Graph>
class JumpingEstimator {
private:
	typedef typename Graph::EdgeId EdgeId;
	const PairedInfoIndex<Graph>& histogram_;
public:
	JumpingEstimator(const PairedInfoIndex<Graph>& histogram) : histogram_(histogram) {
	}

	void Estimate(PairedInfoIndex<Graph> &result) {
		for(auto it = histogram_.begin(); it != histogram_.end(); ++it) {
			vector<PairInfo<EdgeId> > infos = *it;
			double forward = 0;
			for(auto pi_it = infos.begin(); pi_it != infos.end(); ++pi_it)
				if(pi_it->d > 0)
					forward += pi_it->weight;
			if(forward > 0)
				result.AddPairInfo(PairInfo<EdgeId>(infos[0].first, infos[0].second, 1000000, forward, 0));
		}
	}
};

}

#endif /* DISTANCE_ESTIMATION_HPP_ */
