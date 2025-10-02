import React from 'react';
import Editor from '../../components/editor';

import { makeStyles } from '@material-ui/core/styles';
import { graphql, navigate } from 'gatsby';
import { Match } from '@reach/router';

import PipyLogo from '../../images/pipy.svg';

// CSS styles
const useStyles = makeStyles(theme => ({
  root: {
    position: 'absolute',
    top: 0,
    bottom: 0,
    left: 0,
    right: 0,
    display: 'flex',
    flexDirection: 'row',
    flexWrap: 'nowrap',
    alignItems: 'stretch',
  },
  tab: {
    width: theme.TAB_WIDTH,
  },
  home: {
    width: theme.TAB_WIDTH,
    height: theme.TOOLBAR_HEIGHT,
    display: 'flex',
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
  },
  logo: {
    width: '26px',
    cursor: 'pointer',
    transition: 'opacity 0.5s',
    '&:hover': {
      opacity: '60%',
    },
  },
  main: {
    width: `calc(100% - ${theme.TAB_WIDTH}px)`,
  },
}));

export const query = graphql`
  query {
    allDts {
      nodes {
        filename
        internal {
          content
        }
      }
    }
  }
`;

function Repo({ data }) {
  const classes = useStyles();

  const dts = Object.fromEntries(
    data.allDts.nodes.map(
      ({ filename, internal }) => [filename, internal.content]
    )
  );
  return (
    <Match path="/repo/*">
      {props => props.match && (
        <div className={classes.root}>
          <div className={classes.tab}>
            <div className={classes.home}>
              <img
                src={PipyLogo}
                alt="Home"
                className={classes.logo}
                onClick={() => navigate('/')}
              />
            </div>
          </div>
          <div className={classes.main}>
            <Editor
              root={'/' + props.match['*']}
              dts={dts}
            />
          </div>
        </div>
      )}
    </Match>
  );
}

export default Repo;
