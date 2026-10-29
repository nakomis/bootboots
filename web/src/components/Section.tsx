import ExpandMoreIcon from '@mui/icons-material/ExpandMore';
import Accordion from '@mui/material/Accordion';
import AccordionDetails from '@mui/material/AccordionDetails';
import AccordionSummary from '@mui/material/AccordionSummary';
import Typography from '@mui/material/Typography';
import { type ReactNode, useState } from 'react';

/** A collapsible panel (the old pages' ▶ headers). */
export function Section({
  title,
  defaultExpanded = false,
  children,
}: {
  title: string;
  defaultExpanded?: boolean;
  children: ReactNode;
}) {
  const [expanded, setExpanded] = useState(defaultExpanded);
  return (
    <Accordion expanded={expanded} onChange={(_, open) => setExpanded(open)} disableGutters sx={{ mt: 2 }}>
      <AccordionSummary expandIcon={<ExpandMoreIcon />}>
        <Typography variant="subtitle1" fontWeight="bold">
          {title}
        </Typography>
      </AccordionSummary>
      <AccordionDetails>{children}</AccordionDetails>
    </Accordion>
  );
}

/** A labelled row with a control on the right. */
export function SettingRow({
  label,
  help,
  children,
}: {
  label: ReactNode;
  help?: ReactNode;
  children: ReactNode;
}) {
  return (
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        gap: 16,
        marginBottom: 8,
      }}
    >
      <div>
        <Typography variant="body2" fontWeight="bold">
          {label}
        </Typography>
        {help && (
          <Typography variant="caption" color="text.secondary" component="p">
            {help}
          </Typography>
        )}
      </div>
      {children}
    </div>
  );
}
