export type BoundingBox = {
  xmin: number;
  ymin: number;
  xmax: number;
  ymax: number;
};

export type BoundingPoint = {
  x: number;
  y: number;
  /** 1 = cat, 0 = background */
  label: number;
};

/** A row in the `catadata` table (partition key imageName, sort key uuid). */
export type CatadataRecord = {
  imageName: string;
  uuid: string;
  user?: string;
  cat?: string;
  claimedAt?: string;
  reviewedAt?: string;
  boundingBox?: BoundingBox;
  boundingPoints?: BoundingPoint[];
  boundedAt?: string;
  boundedBy?: string;
};
