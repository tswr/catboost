#include "approx_updater_helpers.h"



using namespace NCB;

static void NormalizeLeafValues(const TVector<double>& leafWeightsSum, TVector<double>* leafValues) {
    double averageLeafValue = 0;
    for (size_t leafIdx : xrange(leafWeightsSum.size())) {
        averageLeafValue += (*leafValues)[leafIdx] * leafWeightsSum[leafIdx];
    }
    averageLeafValue /= Accumulate(leafWeightsSum, /*val*/0.0);
    for (auto& leafValue : *leafValues) {
        leafValue -= averageLeafValue;
    }
}

void NormalizeLeafValues(
    bool isPairwise,
    double learningRate,
    const TVector<double>& leafWeightsSum,
    TVector<TVector<double>>* treeValues
) {
    if (isPairwise) {
        NormalizeLeafValues(leafWeightsSum, &(*treeValues)[0]);
    }

    for (auto& treeDimension : *treeValues) {
        for (auto& leafValue : treeDimension) {
            leafValue *= learningRate;
        }
    }
}
