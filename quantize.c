#include "colourspace.h"
#include "quantize.h"

static inline void QuantCluster_ClearTraining(struct QuantCluster_t *x)
{
	x->nPoints = 0;
	x->Train = x->DistCenter = x->DistWeight = (struct BGRAf_t){0,0,0,0};
}

static inline void QuantCluster_Train(struct QuantCluster_t *Dst, const struct BGRAf_t *Data)
{
	struct BGRAf_t Dist = BGRAf_Sub( Data, &Dst->Centroid);
	               Dist = BGRAf_Mul(&Dist, &Dist);
	struct BGRAf_t wData = BGRAf_Mul(Data, &Dist);
	Dst->nPoints++;
	Dst->Train       = BGRAf_Add(&Dst->Train, Data);
	Dst->DistCenter  = BGRAf_Add(&Dst->DistCenter, &wData);
	Dst->DistWeight  = BGRAf_Add(&Dst->DistWeight, &Dist);
}

static inline int QuantCluster_Resolve(struct QuantCluster_t *x)
{
	if(x->nPoints) x->Centroid = BGRAf_Divi(&x->Train, x->nPoints);
	return x->nPoints;
}

static inline void QuantCluster_Split(struct QuantCluster_t *Clusters, int SrcCluster, int DstCluster, const struct BGRAf_t *Data, int nData, int32_t *DataClusters)
{
	Clusters[DstCluster].Centroid = BGRAf_DivSafe(&Clusters[SrcCluster].DistCenter, &Clusters[SrcCluster].DistWeight, &Clusters[SrcCluster].Centroid);

	int n;
	QuantCluster_ClearTraining(&Clusters[SrcCluster]);
	QuantCluster_ClearTraining(&Clusters[DstCluster]);
	for(n=0;n<nData;n++)
	{
		if(DataClusters[n] == SrcCluster)
		{
			float DistSrc = BGRAf_ColDistance(&Data[n], &Clusters[SrcCluster].Centroid);
			float DistDst = BGRAf_ColDistance(&Data[n], &Clusters[DstCluster].Centroid);
			if(DistSrc < DistDst)
			{
				QuantCluster_Train(&Clusters[SrcCluster], &Data[n]);
			}
			else
			{
				QuantCluster_Train(&Clusters[DstCluster], &Data[n]);
				DataClusters[n] = DstCluster;
			}
		}
	}
	QuantCluster_Resolve(&Clusters[SrcCluster]);
	QuantCluster_Resolve(&Clusters[DstCluster]);
}

static inline float SplitDistortionMetric(const struct QuantCluster_t *x)
{
	return BGRAf_Len2(&x->DistWeight);
}

static int QuantCluster_InsertToDistortionList(struct QuantCluster_t *Clusters, int Idx, int Head)
{
	int Next = -1;
	int Prev = Head;
	float Dist = SplitDistortionMetric(&Clusters[Idx]);
	if(Dist != 0.0f)
	{
		while(Prev != -1 && Dist < SplitDistortionMetric(&Clusters[Prev]))
		{
			Next = Prev;
			Prev = Clusters[Prev].Prev;
		}
		Clusters[Idx].Prev = Prev;
		if(Next != -1) Clusters[Next].Prev = Idx;
		else Head = Idx;
	}
	return Head;
}

void QuantCluster_Quantize(struct QuantCluster_t *Clusters, int nCluster, const struct BGRAf_t *Data, int nData, int32_t *DataClusters, int nPasses)
{
	int i, j;
	if(!nData) return;

	QuantCluster_ClearTraining(&Clusters[0]);
	for(i=0;i<nData;i++)
	{
		DataClusters[i] = 0;
		Clusters[0].Centroid = BGRAf_Add(&Clusters[0].Centroid, &Data[i]);
	}
	Clusters[0].Centroid = BGRAf_Divi(&Clusters[0].Centroid, nData);;

	QuantCluster_ClearTraining(&Clusters[0]);
	for(i=0;i<nData;i++)
	{
		QuantCluster_Train(&Clusters[0], &Data[i]);
	}
	if(BGRAf_Len2(&Clusters[0].DistWeight) == 0.0f)
		return;
	Clusters[0].Prev = -1;

	int nClusterCur = 1;
	int MaxDistCluster = 0;
	int EmptyCluster = -1;
	while(MaxDistCluster != -1 && nClusterCur < nCluster)
	{
		int N = nClusterCur;
		for(i=0; i<N; i++)
		{
			int DstCluster = EmptyCluster;
			if(DstCluster == -1) DstCluster = nClusterCur++;
			else EmptyCluster = Clusters[DstCluster].Prev;

			int SrcCluster = MaxDistCluster;
			MaxDistCluster = Clusters[SrcCluster].Prev;
			QuantCluster_Split(Clusters, SrcCluster, DstCluster, Data, nData, DataClusters);
			MaxDistCluster = QuantCluster_InsertToDistortionList(Clusters, SrcCluster, MaxDistCluster);
			MaxDistCluster = QuantCluster_InsertToDistortionList(Clusters, DstCluster, MaxDistCluster);

			MaxDistCluster = Clusters[MaxDistCluster].Prev;
			if(MaxDistCluster == -1) break;

			if(nClusterCur >= nCluster) break;
		}

		int Pass;
		for(Pass=0;Pass<nPasses;Pass++)
		{
			for(i=0;i<nClusterCur;i++)
			{
				QuantCluster_ClearTraining(&Clusters[i]);
			}
			for(i=0;i<nData;i++)
			{
				int   BestIdx  = -1;
				float BestDist = 8.0e37f;
				for(j=0; j<nClusterCur; j++)
				{
					float Dist = BGRAf_ColDistance(&Data[i], &Clusters[j].Centroid);
					if(Dist < BestDist) BestIdx = j, BestDist = Dist;
				}
				DataClusters[i] = BestIdx;
				QuantCluster_Train(&Clusters[BestIdx], &Data[i]);
			}

			int nResolves  =  0;
			MaxDistCluster = -1;
			EmptyCluster   = -1;
			for(i=0;i<nClusterCur;i++)
			{
				if(QuantCluster_Resolve(&Clusters[i]))
				{
					MaxDistCluster = QuantCluster_InsertToDistortionList(Clusters, i, MaxDistCluster);
					nResolves++;
				}
				else
				{
					Clusters[i].Prev = EmptyCluster;
					EmptyCluster = i;
				}
			}

			while(EmptyCluster != -1 && MaxDistCluster != -1)
			{
				int SrcCluster = MaxDistCluster; MaxDistCluster = Clusters[SrcCluster].Prev;
				int DstCluster = EmptyCluster;   EmptyCluster   = Clusters[DstCluster].Prev;
				MaxDistCluster = Clusters[SrcCluster].Prev;
				QuantCluster_Split(Clusters, SrcCluster, DstCluster, Data, nData, DataClusters);
				MaxDistCluster = QuantCluster_InsertToDistortionList(Clusters, SrcCluster, MaxDistCluster);
				MaxDistCluster = QuantCluster_InsertToDistortionList(Clusters, DstCluster, MaxDistCluster);
			}
		}
	}
}
